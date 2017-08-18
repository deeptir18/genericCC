#ifndef REMY_TCP_HH
#define REMY_TCP_HH

#include <assert.h>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <thread>

#include "ccc.hh"
#include "remycc.hh"
#include "tcp-header.hh"
#include "udp-socket.hh"
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
using namespace std;

#define packet_size 1280
#define data_size (packet_size-sizeof(TCPHeader))

template <class T>
class CTCP {
public:
  enum ConnectionType{ SENDER, RECEIVER };

private:
  T congctrl;
  UDPSocket socket;
  ConnectionType conntype;

  string dstaddr;
  int dstport;
	int srcport;

  int train_length;

  double _last_send_time;

  int _largest_ack;

  double tot_time_transmitted;
  double tot_delay;
  int tot_bytes_transmitted;
  int tot_packets_transmitted;

  void tcp_handshake();

public:

  CTCP( T s_congctrl, string ipaddr, int port, int sourceport, int train_length ) 
  :   congctrl( s_congctrl ), 
    socket(), 
    conntype( SENDER ),
    dstaddr( ipaddr ),
    dstport( port ),
		srcport( sourceport ),
    train_length( train_length ),
    _last_send_time( 0.0 ),
    _largest_ack( -1 ),
    tot_time_transmitted( 0 ),
    tot_delay( 0 ),
    tot_bytes_transmitted( 0 ),
    tot_packets_transmitted( 0 )
  {
    socket.bindsocket( dstaddr, dstport, srcport );
  }

  CTCP( CTCP<T> &other )
  : congctrl( other.congctrl ),
    socket(),
    conntype( other.conntype ),
    dstaddr( other.dstaddr ),
    dstport( other.dstport ),
		srcport( other.srcport ),
    train_length( other.train_length ),
    _last_send_time( 0.0 ),
    _largest_ack( -1 ),
    tot_time_transmitted( 0 ),
    tot_delay( 0 ),
    tot_bytes_transmitted( 0 ),
    tot_packets_transmitted( 0 )
  {
    socket.bindsocket( dstaddr, dstport, srcport );
  }

  //duration in milliseconds
  void send_data ( double flow_size, bool byte_switched, int32_t flow_id, int32_t src_id );
  void send_fin ( );
  void send_start_flow ( );
  void listen_for_data ( );
};

#include <string.h>
#include <stdio.h>

#include "configs.hh"

using namespace std;

double current_timestamp( chrono::high_resolution_clock::time_point &start_time_point ){
  using namespace chrono;
  high_resolution_clock::time_point cur_time_point = high_resolution_clock::now();
  // convert to milliseconds, because that is the scale on which the
  // rats have been trained
  return duration_cast<duration<double>>(cur_time_point - start_time_point).count()*1000;
}

char *get_ip_str(const struct sockaddr *sa, char *s, size_t maxlen)
{
    switch(sa->sa_family) {
        case AF_INET:
            inet_ntop(AF_INET, &(((struct sockaddr_in *)sa)->sin_addr),
                    s, maxlen);
            break;

        case AF_INET6:
            inet_ntop(AF_INET6, &(((struct sockaddr_in6 *)sa)->sin6_addr),
                    s, maxlen);
            break;

        default:
            strncpy(s, "Unknown AF", maxlen);
            return NULL;
    }

    return s;
}

template<class T>
void CTCP<T>::tcp_handshake() {


  TCPHeader header, ack_header;
  cerr << "in tcp handshake function" << endl;
  // this is the data that is transmitted. A sizeof(TCPHeader) header followed by a sring of dashes
  char buf[packet_size];
  memset(buf, '-', sizeof(char)*packet_size);
  buf[packet_size-1] = '\0';

  header.seq_num = -1;
  header.flow_id = -1;
  header.src_id = -1;
  header.sender_timestamp = -1;
  header.receiver_timestamp = -1;

	// set reuse on the socket
	if ( socket.set_reuse() < 0 ) {
		cout << "Could not set reuse on the socket" << endl;
	} else {
		cout << "Set reuse on socket" << endl;
	}
  sockaddr_in other_addr;
  // first get the IP and the port the client is coming from, wait for "open sesame"
  cout << "Waiting to receive initial data" << endl;
  if ( socket.receivedata( buf, packet_size, 30000, other_addr ) == 0 ) {

    cout << "Did not get an initial packet telling us where client is coming from in 30 seconds" << endl;
		cout << "Keeping socket bound to what is passed in" << endl;
  } else {
    cout << "Received client IP and port and initial open seasame!!!!!" << endl;
		char s;
		get_ip_str( (struct sockaddr *) &other_addr, &s, INET_ADDRSTRLEN);
    dstaddr = string(&s, INET_ADDRSTRLEN);
    dstport = (int) ntohs(other_addr.sin_port);
    cout << "IP: " << dstaddr << ", port: " << dstport << endl;
		socket.close_socket();
  	UDPSocket new_socket;
    new_socket.bindsocket( dstaddr, dstport, srcport );
		socket = new_socket;
	}
  double rtt;
  chrono::high_resolution_clock::time_point start_time_point;
  start_time_point = chrono::high_resolution_clock::now();
  double last_send_time = numeric_limits<double>::min();
  bool multi_send = false;
  while ( true ) {
    double cur_time = current_timestamp(start_time_point);
    if ( cur_time > 60000 ) {
      cout << "Did not establish connection in over 60 seconds" << endl;
      exit(-1);
    }
    //if (last_send_time < cur_time - 1000) {
    memcpy( buf, &header, sizeof(TCPHeader) );
    cout << "Sending handshake packet" << endl;
    socket.senddata( buf, packet_size, NULL );
    if (last_send_time != numeric_limits<double>::min())
	    multi_send = true;
    last_send_time = cur_time;
    //}
    if (socket.receivedata( buf, packet_size, 1000, other_addr ) == 0) {
      cout << "Could not establish connection" << endl;
      continue;
    }
    cout << "Received a packet!" << endl;
    memcpy(&ack_header, buf, sizeof(TCPHeader));
    if (ack_header.seq_num != -1 || ack_header.flow_id != -1)
      continue;
    if (ack_header.sender_timestamp != -1 || ack_header.src_id != -1)
      continue;
    rtt = current_timestamp(start_time_point) - last_send_time;
    break;
  }
  // Set min_rtt only if we are sure we have the right rtt
  if (!multi_send)
    congctrl.set_min_rtt(rtt);
  cout << "Connection Established." << endl; 
}

// takes flow_size in milliseconds (byte_switched=false) or in bytes (byte_switched=true) 
template<class T>
void CTCP<T>::send_data( double flow_size, bool byte_switched, int32_t flow_id, int32_t src_id ){
  cout << "In send data function about to send more data" << endl;
  tcp_handshake();

  TCPHeader header, ack_header;

  // this is the data that is transmitted. A sizeof(TCPHeader) header followed by a sring of dashes
  char buf[packet_size];
  memset(buf, '-', sizeof(char)*packet_size);
  buf[packet_size-1] = '\0';

  // for link logging
  ofstream link_logfile;
  if( LINK_LOGGING )
    link_logfile.open( LINK_LOGGING_FILENAME, ios::out | ios::app );

  // for flow control
  _last_send_time = 0.0;
  double cur_time = 0;
  int seq_num = 0;
  _largest_ack = -1;

  // for estimating bottleneck link rate
  double link_rate_estimate = 0.0;
  double last_recv_time = 0.0;

  // for maintaining performance statistics
  double delay_sum = 0;
  int num_packets_transmitted = 0;
  int transmitted_bytes = 0;

  cout << "Assuming training link rate of: " << TRAINING_LINK_RATE << " pkts/sec" << endl;

  chrono::high_resolution_clock::time_point start_time_point = chrono::high_resolution_clock::now();
  cur_time = current_timestamp( start_time_point );
  _last_send_time = cur_time;
  // For computing timeouts
  double last_ack_time = cur_time;

  congctrl.set_timestamp(cur_time);
  congctrl.init();

  // Get min_rtt from outside
  const char* min_rtt_c = getenv("MIN_RTT");
  congctrl.set_min_rtt(atof(min_rtt_c));

  while ((byte_switched?(num_packets_transmitted*data_size):cur_time) < flow_size) {
    cur_time = current_timestamp( start_time_point );
    if (cur_time - last_ack_time > 1000) {
      std::cerr << "Timeout" << std::endl;
      congctrl.set_timestamp(cur_time);
      congctrl.init();
      _largest_ack = seq_num - 1;
      _last_send_time = cur_time;
      last_ack_time = cur_time; // So we don't timeout repeatedly
    }
    // Warning: The number of unacknowledged packets may exceed the congestion window by num_packets_per_link_rate_measurement
    while (((seq_num < _largest_ack + 1 + congctrl.get_the_window()) &&
	    (_last_send_time + congctrl.get_intersend_time() * train_length <= cur_time) &&
	    (byte_switched?(num_packets_transmitted*data_size):cur_time) < flow_size ) ||
	   (seq_num % train_length != 0)) {
      header.seq_num = seq_num;
      header.flow_id = flow_id;
      header.src_id = src_id;
      header.sender_timestamp = cur_time;
      header.receiver_timestamp = 0;

      memcpy( buf, &header, sizeof(TCPHeader) );
      socket.senddata( buf, packet_size, NULL );
      _last_send_time += congctrl.get_intersend_time();
      if (seq_num % train_length == 0) {
	      congctrl.set_timestamp(cur_time);
	      congctrl.onPktSent( header.seq_num / train_length );
      }

      seq_num++;
    }
    if (cur_time - _last_send_time >= congctrl.get_intersend_time() * train_length ||
	seq_num >= _largest_ack + congctrl.get_the_window()) {
      // Hopeless. Stop trying to compensate.
      _last_send_time = cur_time;
    }

    cur_time = current_timestamp( start_time_point );
    double timeout = _last_send_time + 1000; //congctrl.get_timeout(); // everything in milliseconds
    if(congctrl.get_the_window() > 0)
      timeout = min( 1000.0, _last_send_time + congctrl.get_intersend_time()*train_length - cur_time );
    if ( timeout < 0 ) {
      timeout = congctrl.get_intersend_time()*train_length;
    }

    sockaddr_in other_addr;
    if(socket.receivedata(buf, packet_size, timeout, other_addr) == 0) {
      cur_time = current_timestamp(start_time_point);
      if(cur_time > _last_send_time + congctrl.get_timeout())
        congctrl.onTimeout();
      continue;
    }
    
    memcpy(&ack_header, buf, sizeof(TCPHeader));
    //cout << "Received ack # " << ack_header.seq_num << endl;
    ack_header.seq_num++; // because the receiver doesn't do that for us yet
    
    if (ack_header.src_id != src_id || ack_header.flow_id != flow_id){
      if(ack_header.src_id != src_id ){
        std::cerr<<"Received incorrect ack for src "<<ack_header.src_id<<" to "<<src_id<<" for flow "<<ack_header.flow_id<<" to "<<flow_id<<endl;
      }
      continue;
    }
    cur_time = current_timestamp( start_time_point );
    last_ack_time = cur_time;

    // Estimate link rate
    if ((ack_header.seq_num - 1) % train_length != 0 && last_recv_time != 0.0) {
      double alpha = 1 / 16.0;
      if (link_rate_estimate == 0.0)
	link_rate_estimate = 1 * (cur_time - last_recv_time);
      else
	link_rate_estimate = (1 - alpha) * link_rate_estimate + alpha * (cur_time - last_recv_time);
      // Use estimate only after enough datapoints are available
      if (ack_header.seq_num > 2 * train_length)
	congctrl.onLinkRateMeasurement(1e3 / link_rate_estimate );
    }
    last_recv_time = cur_time;
    // Track performance statistics
    delay_sum += cur_time - ack_header.sender_timestamp;
    this->tot_delay += cur_time - ack_header.sender_timestamp;
    
    transmitted_bytes += data_size;
    this->tot_bytes_transmitted += data_size;
    
    num_packets_transmitted += 1;
    this->tot_packets_transmitted += 1;

    if ((ack_header.seq_num - 1) % train_length == 0) {
      congctrl.set_timestamp(cur_time);
      congctrl.onACK(ack_header.seq_num / train_length,
		     ack_header.receiver_timestamp,
		     ack_header.sender_timestamp);
    }
#ifdef SCALE_SEND_RECEIVE_EWMA
    //assert(false);
#endif
    
    _largest_ack = max(_largest_ack, ack_header.seq_num);
  }
  
  cur_time = current_timestamp( start_time_point );
  
  congctrl.set_timestamp(cur_time);
  congctrl.close();
  
  this->tot_time_transmitted += cur_time;
  // send the end flow message
  double throughput = transmitted_bytes/( cur_time / 1000.0 );
  double delay = (delay_sum / 1000) / num_packets_transmitted;

  std::cout<<"\n\nData Successfully Transmitted\n\tThroughput: "<<throughput<<" bytes/sec\n\tAverage Delay: "<<delay<<" sec/packet\n";
  
  double avg_throughput = tot_bytes_transmitted / ( tot_time_transmitted / 1000.0);
  double avg_delay = (tot_delay / 1000) / tot_packets_transmitted;
  std::cout<<"\n\tAvg. Throughput: "<<avg_throughput<<" bytes/sec\n\tAverage Delay: "<<avg_delay<<" sec/packet\n";
  
  if( LINK_LOGGING )
    link_logfile.close();
}

template<class T>
void CTCP<T>::send_fin ( ) {
  cout << "About to send the fins" << endl;
  socket.senddata( "FIN", 3, NULL );
}

template<class T>
void CTCP<T>::send_start_flow ( ) {
  cout << "About to send the start flow" << endl;
  socket.senddata( "START_FLOW", 10, NULL );
}

template<class T>
void CTCP<T>::listen_for_data ( ){

}

#endif
