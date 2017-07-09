/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2017 Kungliga Tekniska Högskolan   
 *               2017 Universita' degli Studi di Napoli Federico II
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * HHF, The Heavy-Hitter Filter Queueing discipline
 * 
 * This implementation is based on linux kernel code by
 * Copyright (C) 2013 Terry Lam <vtlam@google.com>
 * Copyright (C) 2013 Nandita Dukkipati <nanditad@google.com>
 *
 * Implemented in ns-3 by: Surya Seetharaman <suryaseetharaman.9@gmail.com>
 *                         Stefano Avallone <stavallo@unina.it>
 */

#ifndef HHF_QUEUE_DISC
#define HHF_QUEUE_DISC

#include "ns3/drop-tail-queue.h"
#include "ns3/queue-disc.h"
#include "ns3/nstime.h"
#include "ns3/boolean.h"
#include "ns3/object.h"
#include "fq-codel-queue-disc.h"
#include "ns3/ipv4-packet-filter.h"
#include "ns3/ipv6-packet-filter.h"
#include <list>
#include <random>

#define HH_FLOW_CNT 1024     //!< number of flow entries in the flow-table T
#define HHF_ARRAYS_CNT 4     //!< number of counter arrays in the multistage filter F
#define HHF_ARRAYS_LEN 1024  //!< number of counters in each array of F
#define HHF_BIT_MASK_LEN 10  //!< masking 10 bits for counter array index
#define HHF_BIT_MASK 0x3FF   //!< bitmask of 10 bits
#define WDRR_BUCKET_CNT 2    //!< two buckets for the Weighted DRR

namespace ns3 {

class TraceContainer;
class UniformRandomVariable;

/**
 * \ingroup traffic-control
 *
 * \brief Implements HHF Queue Management discipline
 */
class HhfQueueDisc : public QueueDisc
{
public:
  /**
   * \brief Get the type ID.
   * \return the object TypeId
   */
  static TypeId GetTypeId (void);

  /**
   * \brief HhfQueueDisc Constructor
   */
  HhfQueueDisc ();

  /**
   * \brief HhfQueueDisc Destructor
   */
  virtual ~HhfQueueDisc ();

private:
  class ListHead
  {
  public:
    /**
     * \brief ListHead constructor
     */
    ListHead ();

    virtual ~ListHead ();

    //functions to do listhead operations.
    virtual bool ListEmpty (void);
    virtual void ListAddTail (ListHead *newHead);
    virtual void ListMoveTail (ListHead *list);
    virtual void ListDelete (ListHead *entry);
    virtual void InitializeListHead (ListHead *list);
    //virtual void InitializeListHead ();
    virtual int ListIsLast (ListHead *head);

    ListHead *prev;
    ListHead *next;

  };

public:
  /**
   * \brief heavy-hitter per flow state
   */
  struct FlowState
  {
    uint32_t hashId;         //!< hash of the flow-id (like TCP 5-tuple)
    Time hitTimeStamp;       //!< last time heavy-hitter was seen
    ListHead flowChain;      //!< chaining under the hash collision
  };

  /**
   * \brief Weighted Deficit Round Robin (WDRR) scheduler
   */
  struct WdrrBucket
  {
    Ptr<InternalQueue> packetQueue;  //pointer to the FIFO (droptail) queue to store packets
    ListHead bucketChain;            //!< circular doubly linked list node
    int32_t deficit;                 //!< weight of the bucket * quantum
  };


  /**
   * \brief Bucket types
   */
  enum WdrrBucketIndex
  {
    WDRR_BUCKET_FOR_HH     = 0, //!< bucket id for heavy hitters 
    WDRR_BUCKET_FOR_NON_HH = 1, //!< bucket id for non-heavy-hitters
  };

  /**
   * \brief Enumeration of the modes supported in the class.
   *
   */
  enum QueueDiscMode
  {
    QUEUE_DISC_MODE_PACKETS,     /**< Use number of packets for maximum queue disc size */
    QUEUE_DISC_MODE_BYTES,       /**< Use number of bytes for maximum queue disc size */
  };

  /**
   * \brief Set the operating mode of this queue disc.
   *
   * \param mode The operating mode of this queue disc.
   */
  void SetMode (QueueDiscMode mode);

  /**
   * \brief Get the operating mode of this queue disc.
   *
   * \returns The operating mode of this queue disc.
   */
  QueueDiscMode GetMode (void);

  /**
   * \brief Get the current value of this queue disc in bytes or packets.
   *
   * \returns The queue disc size in bytes or packets.
   */
  uint32_t GetDiscSize (void);

  /**
   * \brief Set the limit of the queue in bytes or packets.
   *
   * \param lim The limit in bytes or packets.
   */
  void SetQueueLimit (uint32_t lim);

  /**
   * \brief Set the quantum value.
   *
   * \param quantum The number of bytes each queue gets to dequeue on each round of the scheduling algorithm.
   */
  void SetQuantum (uint32_t quantum);

  /**
   * \brief Get the quantum value.
   *
   * \returns The number of bytes each queue gets to dequeue on each round of the scheduling algorithm.
   */
  uint32_t GetQuantum (void) const;

  /**
   * \brief Get the bucket.
   *
   * \param list The pointer to ListHead.
   * \returns the bucket containing the head node of the list.
   */
  WdrrBucket* ListFirstEntry (ListHead* list);

protected:
  /**
   * \brief Dispose of the object
   */
  virtual void DoDispose (void);

private:

  virtual bool DoEnqueue (Ptr<QueueDiscItem> item);
  virtual Ptr<QueueDiscItem> DoDequeue (void);
  virtual Ptr<const QueueDiscItem> DoPeek (void) const;
  virtual bool CheckConfig (void);
  virtual void InitializeParams (void);
  /**
   * \brief Drop a packet from the head of the bucket (first option is the HH bucket, if its empty then non-HH bucket)
   * \return the index of the bucket (HH or non-HH) from which the packet was dropped.
   */
  uint32_t DoDrop (void);

  /** Assigns packets to WDRR buckets.  Implements a multi-stage filter to
   * classify heavy-hitters.
   */
  WdrrBucketIndex DoClassify (Ptr<QueueDiscItem> item);

  /**
   * Looks up a heavy-hitter flow in a chaining list of table T.
   */
  FlowState* SeekList (uint32_t hash, ListHead *head);

  /**
   * Returns a flow state entry for a new heavy-hitter.
   * Either reuses an expired entry or dynamically allocates a new entry.
   */
  FlowState* AllocNewHh (ListHead *head);

  // ** Variables maintained by HHF
  uint32_t m_limit;                             //!< Maximum number of packets in the queue disc
  QueueDiscMode m_mode;                         //!< Mode (bytes or packets)
  WdrrBucket m_buckets [WDRR_BUCKET_CNT];       //!< the two buckets for WDRR Scheduler
  //uint32_t m_pertubation;                       //!< hash pertubation
  uint32_t m_quantum;                           //!< Deficit assigned to flows at each round
  uint32_t m_dropOverLimit;                     //!< number of times max qdisc packet limit was hit

  ListHead *m_hhFlows [HH_FLOW_CNT];     //!< Flow Table T (contains currently active HHs)
  uint32_t m_hhFlowsLimit;                      //!< max active HH allocs
  uint32_t m_hhFlowsOverLimit;                  //!< number of disallowed HH allocs
  uint32_t m_hhFlowsTotalCnt;                   //!< total admitted HHs
  uint32_t m_hhFlowsCurrentCnt;                 //!< total current HHs

  uint32_t *m_counterArrays [HHF_ARRAYS_CNT];    //!< HHF multistage-filter F
  Time m_arraysResetTimestamp;                  //!< last time counterArrays was reset
  uint32_t *m_validBits [HHF_ARRAYS_CNT];        //!< shadow valid bits in counterArrays

  ListHead m_newBuckets;  //!< The list of new buckets
  ListHead m_oldBuckets;  //!< The list of old buckets

  // ** Variables supplied by user
  Time m_resetTimeout;                       //!< interval to reset the counterArrays in F
  uint32_t m_admitBytes;                     //!< counter threshold to classify as HH
  Time m_evictTimeout;                       //!< aging threshold to evict idle HHs out of the table T.
  uint32_t m_nonHhWeight;                    //!< WDRR wieght for non-HHs

  std::list<FlowState*> m_tmpArray;    //!< stores all the flowstates of the currently active HHs.

};

};   // namespace ns3

#endif
