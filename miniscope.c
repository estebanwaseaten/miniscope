#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <fcntl.h>      //open() and O_RDWR
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <linux/types.h>
#include <linux/spi/spidev.h>

#define PORT        8080
#define MAX_SAMPLES 512

const char *devicePath = "/dev/spidev1.0";
uint8_t mode = SPI_MODE_0;
uint8_t bitsPerWord = 8;
uint32_t speed = 10000000;
int spi_handle;
struct spi_ioc_transfer spiTransfer;
uint8_t tx[2];
uint8_t rx[2];

float gFactor;       // init in main:  = 3.3/4096
float gData[4][MAX_SAMPLES];


static inline uint8_t SPICMD(uint16_t spi)
{
    return (uint8_t)(((spi) >> 8) & 0xFF);
}

static inline uint8_t SPIDATA(uint16_t spi)
{
    return (uint8_t)((spi) & 0xFF);
}

uint16_t SPI_simple_transfer( uint16_t data )
{
	uint32_t ret;
    tx[0] = (data >> 8);
    tx[1] = (uint8_t)data;
    rx[0] = 0; rx[1] = 0;

    if( ioctl(spi_handle, SPI_IOC_MESSAGE(1), &spiTransfer) < 1 )
    {
        printf( "SPI transfer fail\n" );
    }

    uint16_t highbits = rx[0];
    uint16_t lowbits = rx[1];
    uint16_t result = (highbits << 8) + lowbits;
    return result;
}

uint16_t SPI_doubleTransfer( uint16_t cmd )
{
	SPI_simple_transfer( cmd );
	return SPI_simple_transfer( 0x5000 );
}


int SPI_fetch_data( int channel )
{
    uint16_t response = 0;
    response = SPI_doubleTransfer( 0x6000 );    //ask if data is ready on any channels / could also ask if NEW data is ready...
    //response = SPI_simple_transfer( 0x5000 );    //fetch reply for last question

    if( ( SPIDATA( response ) & channel ) == 0 )
    { return -1; } //no new data ready

    //printf( "data ready: 0x%x\n", SPIDATA( response ) );

    response = SPI_doubleTransfer( 0x6200 + channel );      //data transfer request
    //response = SPI_simple_transfer( 0x5000 );                //acknowledged
    int repeats = SPI_simple_transfer( 0x5000 );             //fetch size

    if( repeats > MAX_SAMPLES )
    { repeats = MAX_SAMPLES; }

    for( int i = 0; i < repeats; i++ )
    {
        gData[channel-1][i] = SPI_simple_transfer( 0x5000 )*gFactor;  //query
    }

    response = SPI_doubleTransfer( 0x5F00 );	//abort
    response = SPI_doubleTransfer( 0x6100 );	//start
    return repeats;
}

void SPI_init()
{
    spi_handle = open( devicePath, O_RDWR );

    if( ioctl(spi_handle, SPI_IOC_WR_MODE, &mode) == -1 )
        printf( "SPI error 1\n" );
    if( ioctl(spi_handle, SPI_IOC_WR_BITS_PER_WORD, &bitsPerWord) == -1 )
        printf( "SPI error 2\n" );
    if(  ioctl(spi_handle, SPI_IOC_WR_MAX_SPEED_HZ, &speed) == -1 )
        printf( "SPI error 3\n" );


    spiTransfer.tx_buf = (uint64_t)&tx;    //transmitted
    spiTransfer.rx_buf = (uint64_t)&rx;    //received
    spiTransfer.len = 2;
    spiTransfer.speed_hz = speed;
    spiTransfer.delay_usecs = 0;
    spiTransfer.bits_per_word = bitsPerWord;
}


static int build_json(float *data, int *channelHasData, int n, char *buf, int bufsz )
{
    int pos = 0;
    pos += snprintf(buf + pos, bufsz - pos, "[");
    for( int ch = 0; ch < 4; ch++ )
    {
        if( channelHasData[ch] > 0 )
        {
            pos += snprintf(buf + pos, bufsz - pos, "[");
            for (int i = 0; i < n && pos < bufsz - 20; i++) {
                pos += snprintf(buf + pos, bufsz - pos,
                                i < n - 1 ? "%.4f," : "%.4f", data[ch * MAX_SAMPLES + i]);
            }
            pos += snprintf(buf + pos, bufsz - pos, "],");
        }
    }
    //overwrite last comma:
    pos --;
    pos += snprintf(buf + pos, bufsz - pos, "]");
    return pos;
}


static void serve_data( int fd )
{
    //acquire_data(data, NUM_SAMPLES);
    int count[4];
    // 1. fetch data
    count[0] = SPI_fetch_data( 1 );    //fetch channel 1
    count[1] = SPI_fetch_data( 2 );    //fetch channel 2
    count[2] = SPI_fetch_data( 3 );    //fetch channel 3
    count[3] = SPI_fetch_data( 4 );    //fetch channel 4
    if( (count[0] + count[1] + count[2] + count[3]) < 0 )
    {
        printf( "no data available!\n" );
        return;
    }

    //build json from data --> todo: support multiple channels
    char json[4*(MAX_SAMPLES * 12 + 8)];
    build_json( &gData[0][0], count, MAX_SAMPLES, json, sizeof( json ) );

    //printf( "json: %s\n", json );

    char hdr[128];
    int hlen = snprintf(hdr, sizeof( hdr ),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n\r\n",
        strlen(json));
    write(fd, hdr, hlen);
    write(fd, json, strlen(json));
}

static void serve_file( int fd, const char *path )
{
    FILE *f = fopen( path, "r" );
    if( !f )
    {
        const char *not_found = "HTTP/1.1 404 Not Found\r\nConnection: close\r\n\r\n404 Not Found";
        write( fd, not_found, strlen(not_found) );
        return;
    }

    fseek( f, 0, SEEK_END );
    long size = ftell( f );
    rewind( f );

    char *buffer = malloc( size + 1 );
    fread( buffer, 1, size, f );
    fclose( f );

    char hdr[128];
    int hlen = snprintf( hdr, sizeof( hdr ),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/html; charset=utf-8\r\n"
        "Content-Length: %ld\r\n"
        "Connection: close\r\n\r\n", size);
    write( fd, hdr, hlen );
    write( fd, buffer, size );
    free( buffer );
}



int main( void )
{
    printf( "miniscope 0.0.1\n" );

    //load index.html
    int srv = socket(AF_INET, SOCK_STREAM, 0);
    if( srv < 0 )
    {
        printf("socket error");
        return 1;
    }

    int opt = 1;
    setsockopt( srv, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof( opt ));

    struct sockaddr_in addr = {
    .sin_family      = AF_INET,
    .sin_port        = htons(PORT),
    .sin_addr.s_addr = INADDR_ANY
    };

    if( bind(srv, (struct sockaddr *)&addr, sizeof(addr)) < 0 ) //binds port to socket
    {
        printf("bind error");
        return 1;
    }

    listen( srv, 5 );   //os accepts up to 5 connections
    //printf("Scope server listening on http://0.0.0.0:%d\n", PORT);

    char http_request[512];
    bool running = true;

    struct sockaddr_in client_addr;
    memset( &client_addr, 0, sizeof( client_addr ) );
    socklen_t client_len = sizeof( client_addr );
    char client_ip[INET_ADDRSTRLEN];

    SPI_init();

    gFactor = 3.3/4096;

    while( running )
    {
        int fd = accept( srv, (struct sockaddr *)&client_addr, &client_len );  //blocks and waits for con

        if( fd < 0 )
            continue;

        inet_ntop( AF_INET, &client_addr.sin_addr, client_ip, sizeof( client_ip ) );

        memset( http_request, 0, sizeof( http_request ) );    //clear array
        read( fd, http_request, sizeof( http_request ) - 1 );

        //printf( "request received from: %s", http_request );
        //printf( "IP: %s\n", client_ip );

        if( strncmp( http_request, "GET /data", 9 ) == 0 )
            serve_data( fd );
        else
        {
            //just ignore
            serve_file( fd, "index.html" );
            printf( "request received from: %s", http_request );
            printf( "IP: %s\n", client_ip );
        }
        close( fd );
    }

    return 0;
}
