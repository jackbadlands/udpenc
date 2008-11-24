/*
 * udpenc.c: Encrypt UDP packets
 *
 * Copyright (c) 2008 Vitaly "_Vi" Shukela. Some rights reserved.
 * 
 *
 */

static const char rcsid[] = "$Id:$";

#include <stdio.h>   /* reading key file, stderr fprintfs */
#include <stdlib.h>  /* exit */
#include <string.h>  /* memset */
#include <sys/socket.h>   
#include <sys/types.h>   
#include <netinet/in.h>
#include <arpa/inet.h>

#include "blowfish.h"

#define BSIZE 4096
#define KEYSIZE 64 /* in bytes */

#define TRAILMAGIC "\xf7\x73\x77\x2a\x07\xa4\x5c\x78"
#define TRAILLEN 8

#define p_key_name            argv[1]
#define p_plaintext_mode      argv[2]
#define p_plaintext_address   argv[3]
#define p_plaintext_port      argv[4]
#define p_cipher_mode         argv[5]
#define p_cipher_address      argv[6]
#define p_cipher_port         argv[7]

void read_key(const char* fname, BLOWFISH_CTX* ctx);
int get_server_udp_socket(const char* address, int port);
int accept_server_udp_socket(int s, char *buf, int *len);
int get_client_udp_socket(const char* address, int port);
void get_packet(int s_from, char* buf, int *len);
void encrypt(BLOWFISH_CTX* ctx, char* buf, int *len);
void decrypt(BLOWFISH_CTX* ctx, char* buf, int *len);

#define max(a,b) (((a)>(b))?(a):(b))

int main(int argc, char* argv[]){

    BLOWFISH_CTX ctx;

    int plaintext_socket; // user connects here
    int cipher_socket; // encrypted channel here

    fd_set rfds;

    char buf[BSIZE];
    int len;


    if(argc<=7){
	fprintf(stderr,"Usage: udpenc key_file {c|l}[oonect|isten] plaintext_address plaintext_port {c|l}[oonect|isten] cipher_address cipher_port\nExample: \"udpenc secret.key l 127.0.0.1 22 l 192.168.0.1 22\" on one side and \"udpenc secret.key l 127.0.0.1 22 c 192.168.0.1 22\" on the other.\n");
	exit(1);
    }
    if(
	    (*p_plaintext_mode!='c'&&*p_plaintext_mode!='l') ||
	    (*p_plaintext_mode!='c'&&*p_plaintext_mode!='l') ) {
	fprintf(stderr, "Only 'l' or 'c' should be as 2'nd and 5'th argument\n");
	exit(1);
    }

    read_key(p_key_name, &ctx);
    
    // Preparing sockets, phase 1: Create sockets

    if(*p_plaintext_mode=='c'){
	plaintext_socket=get_client_udp_socket(p_plaintext_address,      atoi(p_plaintext_port));
    }else{
	plaintext_socket=get_server_udp_socket(p_plaintext_address,      atoi(p_plaintext_port));
    }

    if(*p_cipher_mode=='c'){
	cipher_socket=get_client_udp_socket(p_cipher_address, atoi(p_cipher_port));
    }else{
	cipher_socket=get_server_udp_socket(p_cipher_address, atoi(p_cipher_port));
    }

    {
	char buf_pl[BSIZE];
	int  len_pl;
	char buf_ci[BSIZE];
	int  len_ci;

	// Preparing sockets, phase 2: Wait for clients for listening sockets, send empty message for connecting ones
	fprintf(stderr,"1\n");

	if(*p_plaintext_mode=='c'){
	    write(plaintext_socket,"",1);	
	}else{
	    accept_server_udp_socket(plaintext_socket, buf_pl, &len_pl);
	}

	if(*p_cipher_mode=='c'){
	    write(cipher_socket,"",1);	
	}else{
	    accept_server_udp_socket(cipher_socket, buf_ci, &len_ci);
	}

	// Preparing sockets, phase 3: Process first messages

	if(*p_cipher_mode=='l'){
	    decrypt(&ctx, buf_ci, &len_ci);
	    write(plaintext_socket, buf_ci, &len_ci);
	}

	if(*p_plaintext_mode=='l'){
	    encrypt(&ctx, buf_pl, &len_pl);
	    write(cipher_socket, buf_pl, len_pl);
	}
    }

    for(;;){
        FD_ZERO(&rfds);
	FD_SET(plaintext_socket, &rfds);
	FD_SET(cipher_socket, &rfds);

	if(select(max(plaintext_socket, cipher_socket)+1, &rfds, NULL, NULL, NULL)<0){
	    perror("select");
	    exit(2);
	}

	if(FD_ISSET(plaintext_socket, &rfds)){
	    get_packet(plaintext_socket, buf, &len);
	    encrypt(&ctx, buf, &len);
	    write(cipher_socket, buf, len);
	}
	
	if(FD_ISSET(cipher_socket, &rfds)){
	    get_packet(cipher_socket, buf, &len);
	    decrypt(&ctx, buf, &len);
	    write(plaintext_socket, buf, len);
	}
    }

}

void get_packet(int s_from, char* buf, int *l){
    
    *l=read(s_from, buf, BSIZE);
    if(*l<0){
	exit(0);
    }
}

void forward_packet_special(char* buf, int len, int s_to, int decrypt){
    write(s_to, buf, len);
}

void setup_socket(int* s, struct sockaddr_in* addr, const char* address, int port){
    *s = socket(AF_INET, SOCK_DGRAM, 0);
    int one = 1;
    setsockopt(*s, SOL_SOCKET, SO_REUSEADDR, (const char *) &one, sizeof(one));
    
    memset(addr, 0, sizeof(struct sockaddr_in));
    addr->sin_family      = AF_INET;
    addr->sin_addr.s_addr = inet_addr(address);
    addr->sin_port        = htons(port);
}

int get_server_udp_socket(const char* address, int port){
    int s; 
    struct sockaddr_in addr;

    setup_socket(&s, &addr, address, port);
    if(bind(s, (struct sockaddr *) &addr, sizeof(addr)) != 0){
	perror("bind");
	exit(2);
    }

    return s;
}

int accept_server_udp_socket(int s, char *buf, int *len){
    struct sockaddr_in addr;
    int size=sizeof(addr);
    *len=recvfrom(s, buf, BSIZE, 0, (struct sockaddr *) &addr, &size);
    connect(s, (struct sockaddr *) &addr, sizeof(addr));
}

int get_client_udp_socket(const char* address, int port){
    int s; 
    struct sockaddr_in addr;
    setup_socket(&s, &addr, address, port);
    if(connect(s, (struct sockaddr *) &addr, sizeof(addr)) != 0){
	perror("connect");
	exit(2);
    }
    return s;
}

void read_key(const char* fname, BLOWFISH_CTX* ctx){
    FILE* f;
    unsigned char buf[KEYSIZE];

    if(fname[0]=='-' && fname[1]==0){
	f=stdin;
    }else{
	f=fopen(fname, "r");
	if(!f){
	    perror("fopen");
	    exit(1);
	}
    }

    if(fread(&buf, KEYSIZE, 1, f)!=1){
	fprintf(stderr, "Error reading key, it must be at least %d bytes\n", KEYSIZE);
	exit(1);
    }

    fclose(f);

    Blowfish_Init (ctx, buf, KEYSIZE);
}

void encrypt(BLOWFISH_CTX* ctx, char* buf, int *len){
    int i;
    if(*len<8){
	for(i=0; i<TRAILLEN; ++i){
	    buf[*len+i]=TRAILMAGIC[i];
	}
	*len+=TRAILLEN;
    }
    for(i=0; i < *len-8; i+=1){
	//fprintf(stderr,"encrypt %d %08X%08X -> ",i, *(unsigned long*)(buf+i), *(unsigned long*)(buf+i+4));
	Blowfish_Encrypt(ctx, (unsigned long*)(buf+i), (unsigned long*)(buf+i+4));
	//fprintf(stderr,"%08X%08X\n",  *(unsigned long*)(buf+i), *(unsigned long*)(buf+i+4));
    }
}
void decrypt(BLOWFISH_CTX* ctx, char* buf, int *len){
    int i;
    for(i=*len-8-1; i >= 0; i-=1){
	//fprintf(stderr,"decrypt %d %08X%08X -> ",i, *(unsigned long*)(buf+i), *(unsigned long*)(buf+i+4));
	Blowfish_Decrypt(ctx, (unsigned long*)(buf+i), (unsigned long*)(buf+i+4));
	//fprintf(stderr,"%08X%08X\n",  *(unsigned long*)(buf+i), *(unsigned long*)(buf+i+4));
    }
    if(*len >= TRAILLEN && *len<8+TRAILLEN){
	*len-=TRAILLEN;     
	for(i=0; i<TRAILLEN; ++i){
	    if(buf[*len+i]!=TRAILMAGIC[i]){
		*len+=TRAILLEN;
		break;
	    }
	}
    }
}
