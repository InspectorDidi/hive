#include "compat.h"

#include "debug.h"
#include "survey_uptime.h"


//*************************************************************
#ifdef WIN32

int GetMacAddr(char* mac)
{
    PIP_ADAPTER_INFO pAdapterInfo = NULL;
    unsigned long size = 0;
    unsigned long status = 0;
	int i;

	//allocate space for the adapter list
    pAdapterInfo = (IP_ADAPTER_INFO*) malloc(sizeof(IP_ADAPTER_INFO));
    size = sizeof(IP_ADAPTER_INFO);
    
	//get the size of the list of network adapters
    status = GetAdaptersInfo(pAdapterInfo, &size);

    if(status != ERROR_SUCCESS)
    {
        if(status == ERROR_BUFFER_OVERFLOW)
        {
			//reallocate space for the adapter list based on the size that
			//was returned by the first GetAdapterInfo call
            free(pAdapterInfo);
            pAdapterInfo = (IP_ADAPTER_INFO*) malloc(size);

			//get the adapter list
            status = GetAdaptersInfo(pAdapterInfo,&size);
            if(status != ERROR_SUCCESS)
            {
                free(pAdapterInfo);
                return -1;
            }
        }
        else
        {
            free(pAdapterInfo);
            return -1;
        }
    }

	//go through the list of network adapters until you find the ethernet
	//adapter
    do 
    {
        if(pAdapterInfo->Type == MIB_IF_TYPE_ETHERNET)
        {       
			for(i = 0; i < 6; ++i)
			{
				mac[i] = pAdapterInfo->Address[i];
			}
			free(pAdapterInfo);
			return 0;
        }
  
		//get next interface
        pAdapterInfo = pAdapterInfo->Next;
    } while (pAdapterInfo);

    free(pAdapterInfo);
    return -1;
}

#elif defined LINUX

#include <sys/socket.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <stdlib.h>			// used for the system() and rand()

#ifdef DEBUG
#include <stdio.h>
#endif

int GetMacAddr( unsigned char *mac )
{
// TODO: encode these strings
	const char	ifname[]="eth0";
    struct 		ifreq ifr;
    int 		fd;
    int 		rv;         // return value - error value from df or ioctl call

#warning
#warning GetMacAddr() is not portable to Linux systems without an "eth0" named interface
#warning

	// TODO: interface name is hardcoded as "eth0"
	/* see Section 17.5, page 468 in Stevens' UNIX Network Programming, Vol 1, Third Edition
	 * for more portable and robust method using SIOCGIFCONF.  On page 469, they start to develop a function
	 * get_ifi_info(), illustrating such a technique, that returns a linked list of all interfaces that are "up".
	 * Also, get_ifi_info() is re-written on page 500 using Routing Sockets
	 */

    /* determine the local MAC address */
    strncpy(ifr.ifr_name, ifname, sizeof(ifname) );

    fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if ( fd < 0 ) 	/* socket open failed */
	{
		D( printf( " ERROR: socket() open failed\n" ); )
		D( perror( "socket" ); )
        rv = FAILURE;	// return failure
	}
    else			/* socket open success */
	{
		D( printf( " DEBUG: socket() open success\n" ); )
        rv = ioctl(fd, SIOCGIFHWADDR, &ifr);

        if (rv >= 0)	/* ioctl success */
		{
			D( printf( " DEBUG: ioctl() success\n" ); )
            memcpy(mac, ifr.ifr_hwaddr.sa_data, IFHWADDRLEN);
			rv = SUCCESS;	// return success
		}
		else			// ioctl failed
		{
			D( printf( " ERROR: ioctl() failed\n" ); )
			rv = FAILURE;	// return failure
		}

		close( fd );
	}

	if ( rv < 0 )
	{
		/* put in junk data into buffer */
		// this OUI corresponds to PRIVATE in the IEEE database
		// if the leading byte is dropped (as it is in how the MAC is transmitted to the LP,
		// AC-DE-48 does not correlate to any vendor in the IEEE OUI database
		mac[0] = 0xAC;
		mac[1] = 0xDE;
		mac[2] = 0x48;

		// lower three byte are psuedo-random
		mac[3] = (unsigned char)( htonl( rand() ) >> 24 );
		mac[4] = (unsigned char)( htonl( rand() ) >> 24 );
		mac[5] = (unsigned char)( htonl( rand() ) >> 24 );
	}


#if	DEBUG
	printf( " DEBUG: MAC address is " );
	int j;

	for ( j = 0; j < IFHWADDRLEN; j++ )
	{
		printf( "%.2X", (unsigned char) *( mac + j ) );
		if ( ( j % 6 ) == 5 )
		{
			printf( "\n" );
		}
		else
		{
			printf( ":" );
		}
	}
#endif

    return rv;		// return success
}

#elif defined SOLARIS

#include <sys/socket.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <stdlib.h>			// used for the system() and rand()
#include <utmpx.h>
#include <time.h>
#include <net/if_arp.h>
#include <sys/sockio.h>

#define IFHWADDRLEN	6

int GetMacAddr( unsigned char *mac )
{

//#warning get_local_hwaddr() is not portable to Solaris systems without an active "hme0" or "e1000g0" named interface

//TODO: encode these strings
//	char	if_e1000g0[] = "e1000g0";
//	char	if_hme0[] = "hme0";
	char	if_lo[] = "lo";
	int 	i, fd, nicount, rv;
	struct	arpreq arpreq;
	struct 	ifreq nicnumber[24];
	struct 	ifconf ifconf;

	if ( ( fd = socket( AF_INET, SOCK_DGRAM, 0 ) ) < 0 )
	{
		D( printf( " ERROR: Could not create socket\n" ); )
		return FAILURE;
	}

	ifconf.ifc_buf = (caddr_t) nicnumber;
	ifconf.ifc_len = sizeof( nicnumber );
        
	if ( ioctl( fd, SIOCGIFCONF, &ifconf ) != 0 )
	{	// error - ioctl() returns zero on success
		close( fd );
		D( printf( " ERROR: ioctl() error\n" ); )
		return FAILURE;
	}

    nicount = ifconf.ifc_len / (sizeof( struct ifreq ) );

	for (i = 0; i <= nicount; i++)
	{ 

		D( printf( " DEBUG: found interface %s\n", nicnumber[i].ifr_name ); )

		if ( strstr( nicnumber[i].ifr_name, if_lo ) == NULL )
		{
			D( printf( " DEBUG: non-loopback interface #%i [%s] found\n", i, nicnumber[i].ifr_name ); )
			break;
		}

#if 0
		// does the interface name match "e1000g0"?
		if ( !strcmp( nicnumber[i].ifr_name, if_e1000g0 ) )
		{	// match found
			D( printf( " DEBUG: interface %i matched 'e1000g0'\n", i ); )
			break;
		}
		// does the interface name match "hme0"?
		else if ( !strcmp( nicnumber[i].ifr_name, if_hme0 ) )
		{	// match found
			D( printf( " DEBUG: interface %i matched 'hme0'\n", i ); )
			break;
		}
#endif

		if ( i == ( nicount - 1 ) )
		{
			close( fd );
			D( printf( " ERROR: No matching interface found\n" ); )
			return FAILURE;
		}
	}

	((struct sockaddr_in*)&arpreq.arp_pa)->sin_addr.s_addr =
	((struct sockaddr_in*)&nicnumber[i].ifr_addr)->sin_addr.s_addr;

	rv = ioctl( fd, SIOCGARP, (char*)&arpreq );
	close( fd );

	if ( rv >= 0 )
	{	//success
		D( printf( " DEBUG: ioctl() success\n" ); )
		rv = SUCCESS;

		memcpy( mac, arpreq.arp_ha.sa_data, IFHWADDRLEN );

	}

	if ( rv < 0 )
	{	// error
		rv = FAILURE;
		D( printf( " DEBUG: ioctl() failed.\n" ); )
		D( printf( " ERROR: Could not determine MAC address.\n" ); )

		/* put in junk data into buffer */
		// this OUI corresponds to PRIVATE in the IEEE database
		// if the leading byte is dropped (as it is in how the MAC is transmitted to the LP,
		// AC-DE-48 does not correlate to any vendor in the IEEE OUI database
		mac[0] = 0xAC;
		mac[1] = 0xDE;
		mac[2] = 0x48;

		// lower three byte are psuedo-random
		mac[3] = (unsigned char)( htonl( rand() ) >> 24 );
		mac[4] = (unsigned char)( htonl( rand() ) >> 24 );
		mac[5] = (unsigned char)( htonl( rand() ) >> 24 );
	}

#if	DEBUG
	printf( " DEBUG: MAC address is " );
	int j;

	for ( j = 0; j < IFHWADDRLEN; j++ )
	{
		printf( "%.2X", (unsigned char) *( mac + j ) );
		if ( ( j % 6 ) == 5 )
		{
			printf( "\n" );
		}
		else
		{
			printf( ":" );
		}
	}
#endif
	
	return rv;
}
#endif