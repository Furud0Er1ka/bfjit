/*
A template interpreter (might be a simple JIT compiler) for brainF running on
x86_64 Linux platform.
*/
#include <bits/stdc++.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
using namespace std;
using memType = uint8_t;

static const memType prologue[] =
    "\x55\x48\x89\xE5\x41\x54\x41\x55\x41\x56\x49\x89\xFC\x49\x89\xF5\x49\x89"
    "\xD6\x48\x81\xEC\x38\x75\x00\x00\x48\x8D\x3c\x24\xBE\x00\x00\x00"
    "\x00\x48\xC7\xC2\x30\x75\x00"
    "\x00\x41\xFF\xD4"
    "\x49\x89\xE4";
/*
0:  55                      push   rbp
1:  48 89 e5                mov    rbp,rsp
4:  41 54                   push   r12
6:  41 55                   push   r13
8:  41 56                   push   r14
a:  49 89 fc                mov    r12,rdi // memset
d:  49 89 f5                mov    r13,rsi // putchar or buffer
10: 49 89 d6                mov    r14,rdx // getchar or buffer
13: 48 81 ec 38 75 00 00    sub    rsp,0x7538
1a: 48 8d 3c 24             lea    rdi,[rsp]
1e: be 00 00 00 00          mov    esi,0x0
23: 48 c7 c2 30 75 00 00    mov    rdx,0x7530
2a: 41 ff d4                call   r12
2d: 49 89 e4                mov    r12,rsp // r12 = tape[ 0 ]
*/
static const memType epilogue[] =
    "\x48\x81\xC4\x38\x75\x00\x00\x41\x5E\x41\x5D\x41\x5C\x5D\xC3";
/*
0:  48 81 c4 38 75 00 00    add    rsp,0x7538
7:  41 5e                   pop    r14
9:  41 5d                   pop    r13
b:  41 5c                   pop    r12
d:  5d                      pop    rbp
e:  c3                      ret
*/
template < size_t N >
inline void pushIns( vector< memType > &vec, const memType ( &add )[ N ] ) {
    for( int i = 0; i < N - 1; i++ )
        vec.emplace_back( add[ i ] );
}
inline void modifyIns( vector< memType > &vec, int offset, int val ) {
    for( int i = 0; i < 4; i++ )
        vec[ offset + i ] = ( val >> ( i << 3 ) ) & 0xff;
}
template < typename T, typename... Ts >
inline void emit( int N, vector< memType > &vec, T val, Ts... args ) {
    for( int i = 0; i < N >> 3; i++ )
        vec.emplace_back( ( val >> ( i << 3 ) ) & 0xff );
    if constexpr( (bool) sizeof...( args ) ) emit( N, vec, args... );
}
struct JIT {
    using mmset = void *( void *, int, size_t );
    using pch = int( int );
    using gch = int();
    vector< memType > ins;
    stack< int, vector< int > > ltb;
    explicit JIT( const char *stream ) {
        pushIns( ins, prologue );
        for( int pc = 0; stream[ pc ] != '\0'; ) {
            switch( stream[ pc ] ) {
            case '>' :
            case '<' : {
                int num = 0;
                for( ;; ) {
                    if( stream[ pc ] == '>' )
                        ++num;
                    else if( stream[ pc ] == '<' )
                        --num;
                    else
                        break;
                    ++pc;
                }
                --pc;
                emit( 8, ins, 0x49, 0x81, 0xc4 );
                emit( 32, ins, num );
                break;
            }
            case '+' :
            case '-' : {
                int num = 0;
                for( ;; ) {
                    if( stream[ pc ] == '+' )
                        ++num;
                    else if( stream[ pc ] == '-' )
                        --num;
                    else
                        break;
                    ++pc;
                }
                --pc;
                emit( 8, ins, 0x41, 0x80, 0x04, 0x24, num );
                break;
            }
            // the operations of the pointer shifts and value changes should be
            // folded to imporove the performance of the JIT compiler.
            case '.' :
                emit( 8, ins, 0x41, 0x0f, 0xb6, 0x3c, 0x24, 0x41, 0xff, 0xd5 );
                break;
                // 0:  41 0f b6 3c 24          movzx  edi,BYTE PTR [r12]
                // 5:  41 ff d5                call   r13
            case ',' :
                emit( 8, ins, 0x41, 0xff, 0xd6, 0x41, 0x88, 0x04, 0x24 );
                break;
                // 0:  41 ff d6                call   r14
                // 3:  41 88 04 24             mov    BYTE PTR [r12],al
            case '[' :
                emit( 8, ins, 0x41, 0x80, 0x3c, 0x24, 0x00, 0x0f, 0x84, 0x00,
                      0x00, 0x00, 0x00 );
                ltb.push( ins.size() );
                break;
                // 0:  41 80 3c 24 00          cmp    BYTE PTR [r12],0x0
                // 5:  0f 84 00 00 00 00       je     0xb
            case ']' :
                emit( 8, ins, 0x41, 0x80, 0x3c, 0x24, 0x00, 0x0f, 0x85, 0x00,
                      0x00, 0x00, 0x00 );
                // 0:  41 80 3c 24 00          cmp    BYTE PTR [r12],0x0
                // 5:  0f 85 00 00 00 00       jne    0xb
                auto e = ltb.top();
                ltb.pop();
                int length = ins.size(), bias = length - e;
                modifyIns( ins, length - 4, -bias );
                modifyIns( ins, e - 4, bias );
                break;
                // loop
            }
            ++pc;
        }
        pushIns( ins, epilogue );
    }
    void run() {
        auto mem = mmap( nullptr, ins.size(), PROT_WRITE | PROT_EXEC,
                         MAP_ANON | MAP_PRIVATE, -1, 0 );
        memcpy( mem, &ins[ 0 ], ins.size() );
        auto fun = (void ( * )( mmset *, pch *, gch * )) mem;
        fun( memset, putchar, getchar );
        munmap( mem, ins.size() );
    }
};
int main( int argc, char **argv ) {
    struct stat s { };
    int fd = open( argv[ 1 ], O_RDONLY ), _ = fstat( fd, &s ), len = s.st_size;
    auto buffer = (char *) mmap( nullptr, len, PROT_READ, MAP_PRIVATE, fd, 0 );
    JIT code( buffer );
    code.run();
    return 0;
}