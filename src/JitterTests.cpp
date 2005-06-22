#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <ctime>

int    N_samples   = 160;

///////////////////////////////////////////////////////////////////////////////

double f_Recorder  = 8000;
double f_Player    = 8000;

//////////////////////////////////////////////////////////////////////////////

inline double abs( double x )
{
    return x < 0 ? -x : x;
    }

inline double sqr( double x )
{
    return x * x;
    }

//////////////////////////////////////////////////////////////////////////////

class TimeQueue;

class Process
{
protected:

    const char* ID;

public:

    static TimeQueue tq;

    virtual bool Task( void* parameter ) = 0;

    Process( const char* id )
        : ID( id )
    {
        }

    const char* GetID( void ) const
    {
        return ID;
        }

    void Enqueue( double tDelta, void* parameter );
    };

class TimeQueue
{
    struct Item
    {
        double tScheduled;
        Process* process;
        void* parameter;
        Item* next;
        };

    double tNow;
    Item* first;

public:

    TimeQueue( void )
        : tNow( 0 )
        , first( NULL )
    {
        }

    double GetTime( void ) const
    {
        return tNow;
        }

    void Enqueue( double tDelta, Process* process, void* parameter )
    {
        Item* newItem = new Item;
        newItem->next = NULL;
        newItem->process = process;
        newItem->parameter = parameter;
        newItem->tScheduled = tNow + tDelta;

        Item* prev = NULL;
        Item* i;

        for( i = first; i; prev = i, i = i->next )
        {
            if ( i->tScheduled > newItem->tScheduled ) // insert 'newItem' before 'i'th
                break;
            }

        newItem->next = i;

        if ( prev )
            prev->next = newItem;
        else
            first = newItem;

        // printf( "%15.6lf %-10s  ->  %.6lf\n", GetTime (), newItem->process->GetID (), newItem->tScheduled );
        }

    bool DoTask( void )
    {
        if ( ! first )
            return false;

        Item* i = first;

        first = first->next;

        tNow = i->tScheduled;

        // printf( "%15.6lf %-10s EXEC\n", GetTime (), i->process->GetID () );

        if ( ! i->process->Task( i->parameter ) )
            return false;

        delete i;

        return true;
        }

    void Initialize( void )
    {
        tNow = 0;
        while( first )
        {
            Item* i = first;
            first = first->next;
            delete i;
            }
        }
    };

inline void Process::Enqueue( double tDelta, void* parameter )
{
    tq.Enqueue( tDelta, this, parameter );
    }

TimeQueue Process::tq;

///////////////////////////////////////////////////////////////////////////////

struct Packet
{
    double tCreation;
    double tNetworkDelay;
    unsigned short SeqNo;
    unsigned long TimeStamp;
    void* data;

    Packet( void )
        : tCreation( 0 )
        , tNetworkDelay( 0 )
        , SeqNo( 0 )
        , TimeStamp( 0 )
        , data( 0 )
    {
        }
    };

///////////////////////////////////////////////////////////////////////////////

class Recorder : public Process
{
    unsigned short SeqNo;
    unsigned long TimeStamp;

    char filename[ 256 ];
    FILE* f;

public:

    Recorder( const char* fn )
        : Process( "Recorder" )
        , f( NULL )
        , SeqNo( 0 )
        , TimeStamp( 0 )
    {
        strcpy( filename, fn );
        }

    ~Recorder( void )
    {
        if ( f )
            fclose( f );
        }

    void Initialize( void )
    {
        if ( f )
        {
            fclose( f );
            f = NULL;
            }

        f = fopen( filename, "rt" );

        SeqNo = rand () & 0xFFFF;
        TimeStamp = SeqNo * N_samples;
        }

    bool Task( void* parameter );
    };

///////////////////////////////////////////////////////////////////////////////

class Network : public Process
{
public:

    Network( void )
        : Process( "Network" )
    {
        }

    void Initialize( void )
    {
        }

    bool Task( void* parameter );
    };

///////////////////////////////////////////////////////////////////////////////

class Receiver : public Process
{
public:

    Receiver( void )
        : Process( "Receiver" )
    {
        }

    void Initialize( void )
    {
        }

    bool Task( void* parameter );
    };

///////////////////////////////////////////////////////////////////////////////

class Player : public Process
{
    unsigned short SeqNo;
    int sampleNo;
    unsigned long TimeStamp;

public:

    Player( void )
        : Process( "Player" )
    {
        }

    void Initialize( void )
    {
        SeqNo = rand () & 0xFFFF;
        sampleNo = rand () % N_samples;
        TimeStamp = SeqNo * N_samples + sampleNo;
        }

    bool Task( void* parameter );

    void SetTimeStamp( unsigned long ts )
    {
        TimeStamp = ts;
        }

    unsigned long GetTimeStamp () const
    {
        return TimeStamp;
        }
    };

///////////////////////////////////////////////////////////////////////////////

class JitterBuffer
{
    int jbf_size;
    int SeqNo[ 1024 ];

    double Hist[ 1024 ];

    double d_Mean;
    double d_Variance;
    double d_MeanAbsDeviation;
    double d_FastMean;
    double quantile;

    unsigned short playSeqNo;
    unsigned short headSeqNo;

    FILE* f_Stat;
    FILE* f_Hist;

public:

    long packetCount;
    long lostPacketCount;
    long discontPacketCount;

    void Initialize( void )
    {
        jbf_size = 1024;

		for ( int i = 0; i < jbf_size; i++ )
        {
			SeqNo[ i ] = 0;
            Hist[ i ] = 0;
            }

        d_Mean = 0;
        d_Variance = 0;
        d_MeanAbsDeviation = 0;
        d_FastMean = 0;

        playSeqNo = 0;
        headSeqNo = 0;

        packetCount = 0;
        lostPacketCount = 0;
        discontPacketCount = 0;
        }

	JitterBuffer( void )
	{
        f_Stat = fopen( "Stat.txt", "wt" );
        f_Hist = fopen( "Hist.txt", "wt" );

        Initialize ();
		}

    ~JitterBuffer( void )
    {
        fclose( f_Stat );
        fclose( f_Hist );
        }

    void ResetStatistics( void )
    {
        packetCount = 0;
        lostPacketCount = 0;
        discontPacketCount = 0;
        }

    void Put( Packet* packet );

    void Get( unsigned short localSeqNo );
    };

///////////////////////////////////////////////////////////////////////////////

Recorder recorder( "data.txt" );
Network network;
Receiver receiver;
Player player;

JitterBuffer jitterBuffer;

///////////////////////////////////////////////////////////////////////////////

bool Recorder::Task( void* none )
{
    if ( ! f )
        return false;

    double delay;
    if ( 1 != fscanf( f, "%lf", &delay ) )
        return false;

    Enqueue( 1.0 / f_Recorder * N_samples, NULL );

    ++SeqNo;
    TimeStamp += N_samples;

    Packet* packet = new Packet;
    packet->tCreation = tq.GetTime ();
    packet->tNetworkDelay = delay;
    packet->SeqNo = SeqNo;
    packet->TimeStamp = TimeStamp;
    packet->data = NULL;

    network.Enqueue( 0.005, packet );
    return true;
    }

bool Network::Task( void* packet_ptr )
{
    Packet* packet = (Packet*) packet_ptr;

    receiver.Enqueue( packet->tNetworkDelay, packet );
    return true;
    }

bool Receiver::Task( void* packet_ptr )
{
    Packet* packet = (Packet*) packet_ptr;

    jitterBuffer.Put( packet );

    delete packet;
    return true;
    }

void JitterBuffer::Put( Packet* packet )
{
    unsigned short remoteSeqNo = packet->SeqNo;
    SeqNo[ remoteSeqNo % jbf_size ] = remoteSeqNo;

    static int AN = 1;

    if ( packetCount == 0 ) // first packet
    {
        d_Mean = 0;
        d_Variance = 0;
        d_MeanAbsDeviation = 0;
        d_FastMean = 0;
        player.SetTimeStamp( remoteSeqNo * N_samples );
        AN = 4;
        }

    ++packetCount;

    long d = player.GetTimeStamp () - remoteSeqNo * N_samples;

    static long d_old[ 256 ] = { 0 };
    for ( int i = 255; i > 0; i-- )
        d_old[ i ] = d_old[ i - 1 ];
    d_old[ 0 ] = d;

    int AN2 = 32;
    d_FastMean = 0;
    for ( i = 0; i < AN2; i++ )
        d_FastMean += d_old[ i ];
    d_FastMean /= AN2;

    double alphaD = 1.0 / AN; //if ( d > d_Mean ) alphaD *= 4;
    d_Mean = d_Mean * ( 1 - alphaD ) + d * alphaD;

    double alphaV = 1.0 / AN;
    d_Variance = d_Variance * ( 1 - alphaV ) + sqr( d - d_Mean ) * alphaV;

    double alphaMAD = 1.0 / AN;
    d_MeanAbsDeviation = d_MeanAbsDeviation * ( 1 - alphaMAD ) + abs( d - d_Mean ) * alphaMAD;

    if ( AN < 256 ) ++AN;

    quantile = d_Mean + sqrt( d_Variance ) * 3 + 1;

    fprintf( f_Stat, 
        "%lf %lf %lf %lf %lf %lf\n",
        double( d ) / N_samples,
        d_Mean / N_samples,
        double( int( quantile / N_samples + 0.5 ) ),
        sqrt( d_Variance ) / N_samples,
        d_MeanAbsDeviation / N_samples,
        d_FastMean / N_samples
        );
    }

void JitterBuffer::Get( unsigned short localSeqNo )
{
    ++playSeqNo;

    short potPlaySeqNo = short( ( player.GetTimeStamp () - quantile ) / N_samples - 1 );
    short delta1 = playSeqNo - potPlaySeqNo;

    if ( abs( delta1  ) >= 2 )
    {
        playSeqNo = potPlaySeqNo;
        discontPacketCount++;
        fprintf( f_Hist, "P" );
        }
    
    bool exist = SeqNo[ playSeqNo % jbf_size ] == playSeqNo;
    if ( ! exist )
        lostPacketCount++;

    ////////////////////////////////////////////////////////////////////////////

    ++headSeqNo;

    short potHeadSeqNo = short( ( player.GetTimeStamp () - d_Mean ) / N_samples );
    short delta = headSeqNo - potHeadSeqNo;

    if ( abs( delta ) >= 2 )
    {
        headSeqNo = potHeadSeqNo;
        fprintf( f_Hist, "H" );
        }

    static int oExist[ 1024 ] = { 0 };

    int j = 0;
    int k = 0;
    for ( int i = 5; i < 32; i++ )
    {
        unsigned short pSN = headSeqNo - i + 10;

        int exist = SeqNo[ pSN % jbf_size ] == pSN ? 1 : 0;

        double alpha = 1.0/16;
        Hist[ i ] = ( 1 - alpha ) * Hist[ i ] + alpha * exist;

        oExist[ i ] = exist;

        if ( Hist[ i ] < 0.99 )
            j = i;

        if ( ( playSeqNo % jbf_size ) == ( pSN % jbf_size ) )
            k = i;
        }
        
    fprintf( f_Hist, "%6hu %6hu %6d (%4d) --", playSeqNo, headSeqNo, lostPacketCount, j - k );

    for ( i = 5; i < 32; i++ )
    {
        fprintf( f_Hist, " %4.lf", 1000 * Hist[ i ] );

        if ( i == k )
            fprintf( f_Hist, "*" );
        if ( i == j )
            fprintf( f_Hist, "+" );
        }

    fprintf( f_Hist, "\n" );
    }

bool Player::Task( void* parameter )
{
    ++TimeStamp;

    if ( ++sampleNo == N_samples )
    {
        ++SeqNo;
        sampleNo = 0;

        jitterBuffer.Get( SeqNo );
        }

    Enqueue( 1.0 / f_Player, NULL );
    return true;
    }

int main( void )
{
    srand( time( NULL ) );

    Process::tq.Initialize ();

    recorder.Initialize ();
    player.Initialize ();
    network.Initialize ();
    receiver.Initialize ();

    jitterBuffer.Initialize ();

    recorder.Enqueue( 0, NULL );
    player.Enqueue( 0, NULL );

    while( Process::tq.DoTask () )
        {}

    return 0;
    }
