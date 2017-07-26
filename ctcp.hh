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

using namespace std;

#define packet_size 1500
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
    socket.bindsocket( ipaddr, port, sourceport );
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
  void send_data ( double flow_size, bool byte_switched, int flow_id, int src_id );

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

template<class T>
void CTCP<T>::tcp_handshake() {
  TCPHeader header, ack_header;

  // this is the data that is transmitted. A sizeof(TCPHeader) header followed by a sring of dashes
  char buf[packet_size];
  memset(buf, '-', sizeof(char)*packet_size);
  buf[packet_size-1] = '\0';

  header.seq_num = -1;
  header.flow_id = -1;
  header.src_id = -1;
  header.sender_timestamp = -1;
  header.receiver_timestamp = -1;


  sockaddr_in other_addr;
  double rtt;
  chrono::high_resolution_clock::time_point start_time_point;
  while ( true ) {
    start_time_point = chrono::high_resolution_clock::now();
    memcpy( buf, &header, sizeof(TCPHeader) );
    socket.senddata( buf, packet_size, NULL );
    if (socket.receivedata( buf, packet_size, 2000, other_addr ) == 0) {
      cerr << "Could not establish connection" << endl;
      continue;
    }
    memcpy(&ack_header, buf, sizeof(TCPHeader));
    if (ack_header.seq_num != -1 || ack_header.flow_id != -1)
      continue;
    if (ack_header.sender_timestamp != -1 || ack_header.src_id != -1)
      continue;
    rtt = current_timestamp( start_time_point );
    break;
  }
  congctrl.set_min_rtt(rtt);
  cout << "Connection Established." << endl; 
}

// takes flow_size in milliseconds (byte_switched=false) or in bytes (byte_switched=true) 
template<class T>
void CTCP<T>::send_data( double flow_size, bool byte_switched, int flow_id, int src_id ){
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
  _last_send_time = 0.0;
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

  congctrl.set_timestamp(cur_time);
  congctrl.init();

  // Get min_rtt from outside
  const char* min_rtt_c = getenv("MIN_RTT");
  congctrl.set_min_rtt(atof(min_rtt_c));

  while ((byte_switched?(num_packets_transmitted*data_size):cur_time) < flow_size) {
    cur_time = current_timestamp( start_time_point );
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
      if ((cur_time - _last_send_time) / (congctrl.get_intersend_time() * train_length) > 10 ||
          seq_num >= _largest_ack + congctrl.get_the_window()) {
        // Hopeless. Stop trying to compensate.
        _last_send_time = cur_time;
      }
      else
        _last_send_time += congctrl.get_intersend_time();
      
      if (seq_num % train_length == 0) {
	congctrl.set_timestamp(cur_time);
	congctrl.onPktSent( header.seq_num );
      }

      seq_num++;
    }

    cur_time = current_timestamp( start_time_point );
    double timeout = _last_send_time + 1000; //congctrl.get_timeout(); // everything in milliseconds
    if(congctrl.get_the_window() > 0)
      timeout = min( timeout, _last_send_time + congctrl.get_intersend_time()*train_length - cur_time );
    
    sockaddr_in other_addr;
    if(socket.receivedata(buf, packet_size, timeout, other_addr) == 0) {
      cur_time = current_timestamp(start_time_point);
      if(cur_time > _last_send_time + congctrl.get_timeout())
        congctrl.onTimeout();
      continue;
    }
    
    memcpy(&ack_header, buf, sizeof(TCPHeader));
    ack_header.seq_num++; // because the receiver doesn't do that for us yet
    
    if (ack_header.src_id != src_id || ack_header.flow_id != flow_id){
      if(ack_header.src_id != src_id ){
        std::cerr<<"Received incorrect ack for src "<<ack_header.src_id<<" to "<<src_id<<" for flow "<<ack_header.flow_id<<" to "<<flow_id<<endl;
      }
      continue;
    }
    cur_time = current_timestamp( start_time_point );

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
      congctrl.onACK(ack_header.seq_num,
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
void CTCP<T>::listen_for_data ( ){

}

#endif
