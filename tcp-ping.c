#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include  <signal.h>
#include <sys/time.h>
#include <stdlib.h>
#include <math.h>
#include <unistd.h>

int sent = 0;					// numero de pings disparados
int count = 1;					// numero de pings maximo
int port = 0;						// porta alvo
int timeout = 1;					// timeout (segundos)
int interval = 0;					// decimos de segundos
int seq = 1;						// id de sequencia
char *target;					// host alvo
struct timeval start, stop;		// timers
long int mc_start, mc_stop;		// microsegundos armazenados
long int mc_min=0, mc_max=0,mc_tot=0;	// tempos medidos
int disarm = 0; // desarmar alarm e impedir equivocos
int received=0;

int ip_version = 4;	// 4=ipv4, 6=ipv6
int addrtype = AF_INET;

struct in6_addr ipv6addr;
struct in_addr ipv4addr;

struct hostent *host;			// host alvo resolvido

// dados da conexao em andamento
int replied = 0;
int sockfd=0;

//--------------------------------------------------------------------------------------------------------


void print_ping(int status);
void usage(void);
long int getmicrotime();
void sigalarm();
void print_ping(int status);

//--------------------------------------------------------------------------------------------------------


void usage(void){
	fprintf(stderr,
		"Usage: tcp-ping [-f] [-c count] [-w timeout] [-p port] [-t interval] destination\n"
		"  -f : quit on first reply\n"
		"  -c count : how many packets to send\n"
		"  -w timeout : how long to wait for a reply\n"
		"  -p port : tcp port\n"
		"  -t interval : how long to wait for a reply from transmited request\n"
		"  -4 : use IPv4 (default)\n"
		"  -6 : use IPv6\n"
		"\n"
		"  destination : ask for what ip address\n"
		);
	exit(2);
}

long int getmicrotime(){
	struct timeval tclock;
	long int mts;
	gettimeofday(&tclock, NULL);
	mts = (long int)((tclock.tv_sec*1000000) + tclock.tv_usec);
	return mts;
}

void sigalarm(){
	long int now;
	now = getmicrotime();
	if(disarm){
		alarm(1);
		return;
	}
	// printf("[alarm] Check start %ld open=%d\n", now, sockfd);

	if(sockfd && !replied){
		close(sockfd);

		// sem respostas, conexao aguardando resposta
		print_ping(0);

	}

	alarm(1);
}

// imprimir resultado do ping, status 0=ruim, 1=ok
void print_ping(int status){
	long int diftime = 0;

	if(status){
		// foi!
		if(mc_start && mc_stop){
			if(mc_start > mc_stop){
				diftime=1;
			}else{
				diftime = mc_stop-mc_start;
			}
		}else{
			// sem tempo!? como assim?
			diftime=1;
		}
		received++;

		// tempos
		if(!mc_min || diftime < mc_min) mc_min = diftime;
		if(!mc_max || diftime > mc_max) mc_max = diftime;
		mc_tot += diftime;

		printf("reply seq=%d time=%ld", sent, diftime);

	}else{
		printf("fail seq=%d time=%ld", sent, diftime);
	}
	printf("\n");

	if(interval) usleep(100000*interval);

}

void do_tcp_connect(){
	int cret = 0;
	
	struct sockaddr_in6 server6;
	struct sockaddr_in server4;

	if(ip_version==6){
		// IPv6
		bzero(&server6, sizeof(server6));
		server6.sin6_addr = ipv6addr; // *((struct in6_addr*)host->h_addr);
		server6.sin6_port = htons(port);
		server6.sin6_family = AF_INET6;

		// abrir tomada
		if ((sockfd = socket(AF_INET6, SOCK_STREAM, 0)) == -1) {
			fprintf(stderr,"error=socket problem\n");
			exit(1);
		}

	}else{
		// IPv4
		bzero(&server4, sizeof(server4));
		server4.sin_addr = ipv4addr; // *((struct in_addr*)host->h_addr);
		server4.sin_port = htons(port);
		server4.sin_family = AF_INET;

		// abrir tomada
		if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
			fprintf(stderr,"error=socket problem\n");
			exit(1);
		}

	}

	//printf("[conn] Connect...\n");


	// limpar status
	
	// alerta de timeout, boot
	if(!sent) alarm(1);

	// marcar inicio do tempo
	mc_start = getmicrotime();
	//printf("[conn] Start time %ld\n", mc_start);

	sent++;
	disarm = 0;
	
	if(ip_version == 6){
		cret = connect(sockfd, (struct sockaddr*)&server6, sizeof(struct sockaddr));
	}else{
		cret = connect(sockfd, (struct sockaddr*)&server4, sizeof(struct sockaddr));
	}
	if ( cret == -1) {
		disarm=1;

		// nao conectou, imprimir erro, se for aplicavel fazer isso
		close(sockfd);
		print_ping(0);

	}else{
		disarm=1;
		replied = 1;

		// Conectou!
		close(sockfd);

		// Cronometrar diferenca
		mc_stop = getmicrotime();
		print_ping(1);

		mc_stop = 0;
		mc_start = 0;
	}
	sockfd=0;

}

void finish(){
	int loss = 0;
	long int mc_avg=0;

	if(sent && received && received < sent){
		loss = (int)( ( 100 * sent ) / received );
	}
	if(send>0 && received==0) loss=100;


	printf("statistics transmitted=%d received=%d loss=%d\n", sent, received, loss);
	
	if(received){
		if(mc_tot) mc_avg = (long int)(mc_tot / received);
		printf("times min=%ld avg=%ld max=%ld\n", mc_min, mc_avg, mc_max);
	}
	
	if(sent && !received) exit(1);
}


int main (int argc, char **argv) {
	char tmpstr[INET6_ADDRSTRLEN];
	int ch;
	int c;

// ----------------------------------------------------------------------
// 		"Usage: tcp-ping [-f] [-c count] [-w timeout] [-p port] [-t interval] destination\n"

	while ((ch = getopt(argc, argv, "hf64c:w:p:t:i:")) != EOF) {
		switch(ch) {
		case '4':
			ip_version = 4;
			addrtype = AF_INET;
			break;
		case '6':
			ip_version = 6;
			addrtype = AF_INET6;
			break;
		case 'c':
			count = atoi(optarg);
			break;
		case 'p':
			port = atoi(optarg);
			break;
		case 'w':
			timeout = atoi(optarg);
			break;
		case 't':
			interval = atoi(optarg);
			break;
		case 'h':
		case '?':
		default:
			usage();
		}
	}
	if(!port || port > 65535)port=80;
	if(count < 1) count=4;

	argc -= optind;
	argv += optind;

	if (argc != 1) usage();

	//if(ip_version==6) host.h_addrtype = AF_INET6;

	target = *argv;
	
	if(ip_version==6){
		// obter ipv6
		host = gethostbyname2(target, addrtype);
	}else{
		// obter ipv4
		host = gethostbyname(target);
	}
	
	if( (host) == NULL){
		fprintf(stderr,"error=resolv problem\n");
		exit(2);
	}

	signal(SIGALRM, sigalarm);



	if(ip_version==6){

		// Obter ipv6 (128bits)
		ipv6addr = *((struct in6_addr *)host->h_addr);


		inet_ntop(AF_INET6, *host->h_addr_list, tmpstr, sizeof(tmpstr));
		printf("tcp-ping6 host=%s address=%s port=%d\n", target, tmpstr, port);

		// preencher ip binario	
		// inet_pton(AF_INET6, tmpstr, &(ipv6addr));
		
	}else{
		//printf("tcp-ping host=%s address=%s port=%d\n", target, inet_ntoa(*((struct in_addr *)host->h_addr)), port);

		// Obter ipv4 (32bits)
		ipv4addr = *((struct in_addr *)host->h_addr);

		inet_ntop(AF_INET, *host->h_addr_list, tmpstr, sizeof(tmpstr));
		printf("tcp-ping host=%s address=%s port=%d\n", target, tmpstr, port);

		// preencher ipv4 inteiro
		// ipv4addr = inet_pton(tmpstr);
		
		// inet_pton(AF_INET, tmpstr, void *dst);
		/*
			struct hostent {
				char  *h_name;            // official name of host
				char **h_aliases;         // alias list
				int    h_addrtype;        // host address type
				int    h_length;          // length of address
				char **h_addr_list;       // list of addresses
			}
		*/

	}
	// printf("tcp-ping host=%s address=%s port=%d\n", target, inet_ntoa(*((struct in_addr *)host->h_addr)), port);

	for(c=0;c<count;c++){
		//printf("loop %d\n", c);
		do_tcp_connect();
	}

// ------------ FIM ----------------------------------------------------

	finish();


}

