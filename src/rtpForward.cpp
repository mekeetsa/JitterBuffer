

#include <windows.h>

#include <stdio.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

struct RTP_Packet
{
    RTP_Packet* next;
    unsigned char buffer[ 256 ];
    int length;
    DWORD arrived;
    DWORD to_dispatch_at;
    };

RTP_Packet* first = NULL;
RTP_Packet* last = NULL;

sockaddr_in local;
sockaddr_in remote;

int inUdpSocket = -1;

CRITICAL_SECTION mutex;

volatile int lastRcvdSeqNo = 0;

FILE* logf = NULL;

double avg = 0;
double var = 0;

DWORD __stdcall SenderThread( void* param )
{
    while( 1 )
    {
        Sleep( 1 );
        DWORD now = GetTickCount ();

        EnterCriticalSection( &mutex );

        RTP_Packet* prev = NULL;
        for ( RTP_Packet* p = first; p; prev = p, p = p->next )
        {
            if ( p->to_dispatch_at <= now )
                 break;
            }

        if ( p )
        {
            if ( prev )
                prev->next = p->next;
            else
                first = p->next;
            if ( last == p )
                last = prev;
            }

        LeaveCriticalSection( &mutex );

        if ( p )
        {
            DWORD t = now - p->arrived;
            const double alpha = 1.0 / 16;
            avg = avg * ( 1 - alpha ) + t * alpha;
            double var1 = t - avg;
            if ( var1 < 0 ) var1 = - var1;
            var = var * ( 1 - alpha ) + var1 * alpha;
            fprintf( logf, "%6d %6d %3lu -- %5.2lf %5.2lf %3d\n", lastRcvdSeqNo,
                ( p->buffer[ 1 ] << 8 ) + p->buffer[ 2 ],
                t, avg, var, int( 4 * var / 20.0 + 0.5 ) );

            sendto( inUdpSocket, (char*)p->buffer, p->length, 0, (sockaddr*)&remote, sizeof( remote ) );
            delete p;
            }
        }
    }

int main( int argc, char** argv )
{
    if ( argc < 6 )
    {
        printf( "Usage: rtpForward <locUdp> <remIp> <remUdp> <delayfile> <logfile>\n" );
        return 1;
        }

    WORD wVersionRequested;
    wVersionRequested = MAKEWORD( 1, 1 );

    WSADATA wsaData;
    int err = WSAStartup( wVersionRequested, &wsaData );
    if ( err != 0 ) {
        printf( "no usable winsock.dll" );
        return 1;
    }

    unsigned short localUdpPort = atoi( argv[ 1 ] );
    const char* remoteIpAddr = argv[ 2 ];
    unsigned short remoteUdpPort = atoi( argv[ 3 ] );

    local.sin_family = AF_INET;
    local.sin_addr.s_addr = INADDR_ANY;
    local.sin_port = htons( localUdpPort );

    remote.sin_family = AF_INET;
    remote.sin_addr.s_addr = inet_addr( remoteIpAddr );
    remote.sin_port = htons( remoteUdpPort );

    inUdpSocket = socket( PF_INET, SOCK_DGRAM, IPPROTO_UDP ); // Open inbound UDP socket
    if ( inUdpSocket < 0 )
    {
        return 1;
        }
    else if ( bind( inUdpSocket, (sockaddr*)&local, sizeof( local ) ) != 0 )
    {
        return 1;
        }

    InitializeCriticalSection( &mutex );
    printf( "OK %hu\r\n", localUdpPort ); fflush( stdout );
/*
    int tos = IPTOS_LOWDELAY;

    setsockopt( inUdpSocket, IPPROTO_IP, IP_TOS, &tos, sizeof( tos ) );
*/
    unsigned char pkt[ 4096 ];

    int maxfds = inUdpSocket;

    CreateThread( NULL, 0, SenderThread, 0, 0, NULL );

    FILE* f = fopen( argv[ 4 ], "r+" );
    logf = fopen( argv[ 5 ], "w" );

    for (;;)
    {
        fd_set readfds;
        FD_ZERO( &readfds );
        FD_SET( inUdpSocket, &readfds );

        if ( ! select( maxfds + 1, &readfds, NULL, NULL, NULL ) )
            continue;

        ///////////////////////////////////////////////////////////////////////
        // B Channel from remote DTS
        ///////////////////////////////////////////////////////////////////////
        //
        if ( FD_ISSET( inUdpSocket, &readfds ) )
        {
            sockaddr_in from;
            int fromlen = sizeof( from );
            int retval = recvfrom( inUdpSocket, (char*)pkt, sizeof( pkt ), 0,
                (sockaddr*) &from, &fromlen );

            lastRcvdSeqNo = ( pkt[ 1 ] << 8 ) + pkt[ 2 ];

            double delay;
            if ( 1 != fscanf( f, "%lf", &delay ) )
            {
                fprintf( stderr, "REWINDING -------------\n" );
                delay = 0.01;
                rewind( f );
                }

            EnterCriticalSection( &mutex );

            if ( last )
            {
                last->next = new RTP_Packet;
                last = last->next;
                }
            else
            {
                first = last = new RTP_Packet;
                }

            last->next = NULL;
            memcpy( last->buffer, pkt, retval );
            last->length = retval;
            last->arrived = GetTickCount ();
            last->to_dispatch_at = last->arrived + int( delay * 1000 );

            LeaveCriticalSection( &mutex );
            }
        }

    closesocket( inUdpSocket );

    return 0;
    }
