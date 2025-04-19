#include <stdint.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "../include/globals.h"
#include "../include/network.h"


int32_t hl_connect(dual_format_addr_t* addr) {
    // int32_t server_port = atoi(addr->port);
    
    // struct sockaddr_in server_addr;
        
    // memset(&server_addr, 0, sizeof(server_addr)); // Zero out all fields
    // server_addr.sin_family = AF_INET;             // IPv4
    // server_addr.sin_port = htons(server_port);    // Convert to network byte order

    // // Encode the IP address and store it in the `server_addr.sin_addr` field
    // int res = inet_pton(AF_INET, addr->presentation_ip, &server_addr.sin_addr);

    // if (res <= 0) {
    //     perror("Invalid address or address not supported");
    //     exit(1);
    // }


    // int fd = socket(AF_INET, SOCK_STREAM, 0);
    
    // for(int i = 0; i < 3; i++) {
    //     res = connect(fd, NULL, NULL);
    //     if (res > 0) {
    //         return res;
    //     }
    // }

    // perror("Connection failed");
    // exit(1);

    return 0;
}