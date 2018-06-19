#include <chrono>

#include "cellular_packet_queue.hh"
#include "timestamp.hh"
#include <iostream>
#include <utility>

using namespace std;

int64_t div64_32(int64_t a, int32_t b){
  return a/b;
}

const int32_t FP_MUL = (1<<28);
const int32_t DEC_MUL = 1000000;


CELLULARPacketQueue::CELLULARPacketQueue( const string & args )
  : DroppingPacketQueue(args),
    qdelay_ref_ ( get_arg( args, "qdelay_ref" ) ),
    beta_ ( get_arg( args,  "beta" ) / 100.0),
    mode ( get_arg( args, "mode") ),
    dq_queue ( {0} ),
    real_dq_queue ( {0} ), 
    eq_queue ( {0} ),
    dequeue_events ( {} ),
    credits (0),
	empty_time (0),
    time_occupied (0),
    calc_interval (get_arg( args, "calc_interval" ))
{
  if ( qdelay_ref_ == 0 || beta_==0 || calc_interval == 0) {
    throw runtime_error( "CELLULAR AQM queue must have qdelay_ref, beta, calc_interval" );
  }
}

void CELLULARPacketQueue::_push_back(enum queue_event e, uint32_t tstamp){
    if(dequeue_events.size() >= 1){
        pair<enum queue_event, uint32_t> last = dequeue_events.back();
        if(last.first == QUEUE_FILLED){
            time_occupied += (tstamp - last.second);
        }
    }
    dequeue_events.push_back(make_pair(e, tstamp));
}

void CELLULARPacketQueue::_pop_front(){
    if(dequeue_events.size() >= 2){
        pair<enum queue_event, uint32_t> first = dequeue_events[0];
        pair<enum queue_event, uint32_t> second = dequeue_events[1];
        if(first.first == QUEUE_FILLED){
            time_occupied -= (second.second - first.second);
        }
    }
    if(dequeue_events.size() >= 1)
        dequeue_events.pop_front();
}

void CELLULARPacketQueue::enqueue( QueuedPacket && p )
{


  if ( ! good_with( size_bytes() + p.contents.size(),
        size_packets() + 1 ) ) {
    // Internal queue is full. Packet has to be dropped.
    return;
  } 

  if (mode==1) {
    if (credits >= FP_MUL) {
      //cout<<0;
      credits -= FP_MUL;
    } else {
      if(p.contents[9]!=0) {
        p.contents[5]+=1;
        p.contents[9]-=1;
      } else {
        credits -= FP_MUL;
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

  _push_back(QUEUE_FILLED, now);
}


QueuedPacket CELLULARPacketQueue::dequeue( void )
{
  // We get a callback every dequeue opportunity
  uint32_t now = timestamp();
  
  if(size_packets()==0) {
    return QueuedPacket("arbit", 0);
  }

  // Following runs only if we used the dequeue opportunity
  //double real_dq_rate_, observed_dq_rate_, target_rate;

  if (size_packets() == 1) {
      real_dq_queue.push_back(now);
      _push_back(QUEUE_EMTPY, now);
  }
  else
      _push_back(QUEUE_FILLED, now);

  while(now-dequeue_events[0].second>calc_interval && dequeue_events.size()>1) {
    _pop_front();
  }

  cout<<"Time occ: "<<time_occupied<<"\n";

  while(now-real_dq_queue[0]>calc_interval && real_dq_queue.size()>1 && now>real_dq_queue[1]) {
      real_dq_queue.pop_front();
  }
  real_dq_queue.push_back(now);
  
  QueuedPacket ret = std::move( DroppingPacketQueue::dequeue () );
  while(now-dq_queue[0]>calc_interval && dq_queue.size()>1 && now>dq_queue[1]) {
    dq_queue.pop_front();
  }
  dq_queue.push_back(now);
   
  int32_t delta = 100;   //For stabilitilt delta should be greater than max RTT

  /*
  observed_dq_rate_ = (1.0 * (dq_queue.size()-1))/calc_interval;
  real_dq_rate_ = (1.0 * (real_dq_queue.size()-1))/calc_interval;
  real_dq_rate_ = observed_dq_rate_ / (time_occupied / calc_interval);
  double current_qdelay = (size_packets() + 1) / real_dq_rate_;
  target_rate = 0.96*real_dq_rate_ + beta_ * (real_dq_rate_ / delta) * min(0.0, (qdelay_ref_ - current_qdelay));
  double credit_prob_ = (target_rate /  observed_dq_rate_) * 0.5;
  credit_prob_ = max(0.0,credit_prob_);
  credit_prob_ = min(1.0, credit_prob_);
  credits += credit_prob_;
  if (credits > 5) {
    credits = 5;
  }*/

  int64_t fp_interval_by_delta = div64_32(((int64_t) calc_interval) * FP_MUL, (int32_t) delta);
  int64_t queue_penalty = ((size_packets() + 1) * time_occupied - qdelay_ref_ * (dq_queue.size() - 1));
  if (queue_penalty < 0)
    queue_penalty = 0;

  // due to powers of 10 in calc_interval 
  // and delay_threshold and delta, division by 100 does not loose precision
  int64_t numerator = div64_32((dq_queue.size() - 1) * ((int32_t) delta) * 96 - ((int32_t) 100 * beta_) * queue_penalty, 100);
  if(numerator < 0)
    numerator = 0;

  int64_t fp_numerator = fp_interval_by_delta * numerator;
  int32_t denominator = 2 * time_occupied * (dq_queue.size() - 1);

  int64_t fp_fraction;
  if(denominator == 0)
    fp_fraction = FP_MUL;
  else
    fp_fraction = div64_32(fp_numerator, denominator);

  if(fp_fraction > FP_MUL)
    fp_fraction = FP_MUL;

  credits += fp_fraction;






  printf("fp_interval_by_delta: %ld, queue_penalty: %ld, numerator: %ld, "
	 "fp_numerator: %ld, denominator: %d, fp_fraction: %ld, fraction %.9Lf\n", 
	 fp_interval_by_delta, queue_penalty, numerator, fp_numerator, denominator, fp_fraction, ((long double) fp_fraction) / (0.0L + FP_MUL));

  int64_t _to_print_fraction = div64_32(fp_fraction * DEC_MUL, FP_MUL);
  int64_t _to_print_spare = div64_32(((int64_t) credits) * DEC_MUL, FP_MUL);

  int64_t _to_print_dq_rate = div64_32(((int64_t) (dq_queue.size() - 1)) * DEC_MUL, calc_interval);
  int64_t _to_print_target_rate = div64_32(_to_print_dq_rate * 2 * fp_fraction, FP_MUL);
  printf("_to_print: fraction: %ld, spare %ld, dq_rate %ld, target_rate %ld, credits: %f\n", 
	 _to_print_fraction, _to_print_spare, _to_print_dq_rate, _to_print_target_rate, (credits+0.0) / FP_MUL);
  
  
  if (mode==0)
  { 
    if (credits > FP_MUL) {
      if (ret.contents[ret.contents.size() - 1] == ret.contents[ret.contents.size() - 3]) {
        credits -= FP_MUL;
      }
    } else {
      ret.contents[ret.contents.size() - 1] = ret.contents[ret.contents.size() - 1] + 1;
      ret.contents[ret.contents.size() - 3] = ret.contents[ret.contents.size() - 3] - 1;
    }
  }

  if(credits > 5 * FP_MUL)
    credits = 5 * FP_MUL;

  return ret;
}
