#include <chrono>

#include "cellular_packet_queue.hh"
#include "timestamp.hh"
#include <iostream>

using namespace std;

CELLULARPacketQueue::CELLULARPacketQueue( const string & args )
  : DroppingPacketQueue(args),
    qdelay_ref_ ( get_arg( args, "qdelay_ref" ) ),
    beta_ ( get_arg( args,  "beta" ) / 100.0),
    mode ( get_arg( args, "mode") ),
    dq_queue ( {0} ),
    real_dq_queue ( {0} ), 
    eq_queue ( {0} ),
    credits (5),
	empty_time (0)
{
  if ( qdelay_ref_ == 0 || beta_==0) {
    throw runtime_error( "CELLULAR AQM queue must have qdelay_ref, beta" );
  }
}

void CELLULARPacketQueue::enqueue( QueuedPacket && p )
{


  if ( ! good_with( size_bytes() + p.contents.size(),
        size_packets() + 1 ) ) {
    // Internal queue is full. Packet has to be dropped.
    return;
  } 

  if (mode==1) {
    if (credits > 1) {
      //cout<<0;
      credits-=1;
    } else {
      if(p.contents[9]!=0) {
        p.contents[5]+=1;
        p.contents[9]-=1;
      } else {
        credits-=1;
      }
      //what is this is 0? Problems when there is TCP_CA_RECOVERY
      //p.contents[p.contents.size() - 1] = p.contents[p.contents.size() - 1] + 1;
      //p.contents[p.contents.size() - 3] = p.contents[p.contents.size() - 3] - 1;
    }
  }

  accept( std::move( p ) );
  uint32_t now = timestamp();

  if(eq_queue.size()>20) {
    eq_queue.pop_front();
  }
  eq_queue.push_back(now);


  assert( good() );
}


QueuedPacket CELLULARPacketQueue::dequeue( void )
{
  uint32_t now = timestamp();
  
  if(size_packets()==0) {
    return QueuedPacket("arbit", 0);
  }
  double real_dq_rate_, observed_dq_rate_, target_rate;
  real_dq_rate_ = (1.0 * (real_dq_queue.size()-1))/20.0;
  observed_dq_rate_ = (1.0 * (dq_queue.size()-1))/(20.0);
  if (empty_time>0) {
	int j=int((real_dq_rate_)*(now-empty_time));
	for(int i=0;i<j;i++) {
		real_dq_queue.push_back(empty_time+((i*(now-empty_time))/j));
	}
  }
  if (size_packets()==1) {
	empty_time=now;
  }
  if (size_packets()>1) {
	 empty_time=0;
  }

  while(now-real_dq_queue[0]>20 && real_dq_queue.size()>1 && now>real_dq_queue[1]) {
    real_dq_queue.pop_front();
  }
  real_dq_queue.push_back(now);

  QueuedPacket ret = std::move( DroppingPacketQueue::dequeue () );
  while(now-dq_queue[0]>20 && dq_queue.size()>1 && now>dq_queue[1]) {
    dq_queue.pop_front();
   }
  dq_queue.push_back(now);
   
  double delta = 100.0;   //For stabilitilt delta should be greater than max RTT
  

  real_dq_rate_ = (1.0 * (real_dq_queue.size()-1))/20.0;
  observed_dq_rate_ = (1.0 * (dq_queue.size()-1))/(20.0);
  double current_qdelay = (size_packets() + 1) / real_dq_rate_;
  target_rate = 0.96*real_dq_rate_ + beta_ * (real_dq_rate_ / delta) * min(0.0, (qdelay_ref_ - current_qdelay));
  double credit_prob_ = (target_rate /  observed_dq_rate_) * 0.5;
  credit_prob_ = max(0.0,credit_prob_);
  credit_prob_ = min(1.0, credit_prob_);
  credits += credit_prob_;
  if (credits > 5) {
    credits = 5;
  }
  if (mode==0)
  { 
    if (credits>1) {
      if (ret.contents[ret.contents.size() - 1] == ret.contents[ret.contents.size() - 3]) {
        credits -= 1;
      }
    } else {
      ret.contents[ret.contents.size() - 1] = ret.contents[ret.contents.size() - 1] + 1;
      ret.contents[ret.contents.size() - 3] = ret.contents[ret.contents.size() - 3] - 1;
    }
  }
  return ret;
}
