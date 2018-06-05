/* -*-mode:c++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#ifndef CELLULAR_PACKET_QUEUE_HH
#define CELLULAR_PACKET_QUEUE_HH

#include <random>
#include <thread>
#include "dropping_packet_queue.hh"
#include <deque>
#include <vector>
#include <utility>

using namespace std;

enum queue_event {QUEUE_EMTPY, QUEUE_FILLED};

class CELLULARPacketQueue : public DroppingPacketQueue
{
private:
    //This constant is copied from link_queue.hh.
    //It maybe better to get this in a more reliable way in the future.
    const static unsigned int PACKET_SIZE = 1504; /* default max TUN payload size */

    //Configurable parameters
    uint32_t qdelay_ref_;

    //Internal parameters
    double beta_;
    uint32_t mode;

    //Status variables
    std::deque<uint32_t> dq_queue;
    std::deque<uint32_t> real_dq_queue;
   	std::deque<uint32_t> eq_queue;
   	std::deque<pair<enum queue_event, uint32_t> > dequeue_events;
    double credits;
    int empty_time;
    int32_t time_occupied;
    double calc_interval;
    //Perfect knwoledge

    virtual const std::string & type( void ) const override
    {
        static const std::string type_ { "cellular" };
        return type_;
    }

public:
    CELLULARPacketQueue( const std::string & args );
    void enqueue( QueuedPacket && p ) override;
    QueuedPacket dequeue( void ) override;
    void _push_back(enum queue_event, uint32_t);
    void _pop_front();
};

#endif
