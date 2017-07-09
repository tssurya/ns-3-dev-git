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

#include "ns3/packet-filter.h"
#include "ns3/log.h"
#include "ns3/string.h"
#include "ns3/queue.h"
#include "ns3/simulator.h"
#include "ns3/hhf-queue-disc.h"
#include "ns3/net-device-queue-interface.h"
#include "ns3/random-variable-stream.h"

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("HhfQueueDisc");

NS_OBJECT_ENSURE_REGISTERED (HhfQueueDisc);

TypeId HhfQueueDisc::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::HhfQueueDisc")
    .SetParent<QueueDisc> ()
    .SetGroupName ("TrafficControl")
    .AddConstructor<HhfQueueDisc> ()
    .AddAttribute ("Mode",
                   "Determines unit for QueueLimit",
                   EnumValue (QUEUE_DISC_MODE_BYTES),
                   MakeEnumAccessor (&HhfQueueDisc::SetMode),
                   MakeEnumChecker (QUEUE_DISC_MODE_BYTES, "QUEUE_DISC_MODE_BYTES",
                                    QUEUE_DISC_MODE_PACKETS, "QUEUE_DISC_MODE_PACKETS"))
    .AddAttribute ("QueueLimit",
                   "Queue limit in bytes/packets",
                   UintegerValue (1000),
                   MakeUintegerAccessor (&HhfQueueDisc::SetQueueLimit),
                   MakeUintegerChecker<uint32_t> ())
    .AddAttribute ("ResetTimeout",
                   "Time interval to reset the counterArrays in F in ms",
                   TimeValue (MilliSeconds (40)),
                   MakeTimeAccessor (&HhfQueueDisc::m_resetTimeout),
                   MakeTimeChecker ())
    .AddAttribute ("AdmitBytes",
                   "Counter threshold value to classify as Haevy Hitter in bytes",
                   UintegerValue (128000),
                   MakeUintegerAccessor (&HhfQueueDisc::m_admitBytes),
                   MakeUintegerChecker<uint32_t> ())
    .AddAttribute ("EvictTimeout",
                   "Time aging threshold to evict idle HHs out of the table T in seconds",
                   TimeValue (Seconds (1)),
                   MakeTimeAccessor (&HhfQueueDisc::m_evictTimeout),
                   MakeTimeChecker ())
    .AddAttribute ("NonHHWeight",
                   "WDRR wieght for non-Heavy Hitters",
                   UintegerValue (2),
                   MakeUintegerAccessor (&HhfQueueDisc::m_nonHhWeight),
                   MakeUintegerChecker<uint32_t> ())
    .AddAttribute ("Quantum",
                   "The quantum value for the WDRR algorithm",
                   UintegerValue (0),
                   MakeUintegerAccessor (&HhfQueueDisc::SetQuantum),
                   MakeUintegerChecker<uint32_t> ())
  ;
  return tid;
}

HhfQueueDisc::HhfQueueDisc ()
  : QueueDisc ()
{
  NS_LOG_FUNCTION (this);
}

HhfQueueDisc::~HhfQueueDisc ()
{
  NS_LOG_FUNCTION (this);
}

void
HhfQueueDisc::DoDispose (void)
{
  NS_LOG_FUNCTION (this);
  QueueDisc::DoDispose ();
}

void
HhfQueueDisc::SetMode (QueueDiscMode mode)
{
  NS_LOG_FUNCTION (this << mode);
  m_mode = mode;
}

HhfQueueDisc::QueueDiscMode
HhfQueueDisc::GetMode (void)
{
  NS_LOG_FUNCTION (this);
  return m_mode;
}

void
HhfQueueDisc::SetQueueLimit (uint32_t lim)
{
  NS_LOG_FUNCTION (this << lim);
  m_limit = lim;
}

uint32_t
HhfQueueDisc::GetDiscSize (void)
{
  NS_LOG_FUNCTION (this);
  if (GetMode () == QUEUE_DISC_MODE_BYTES)
    {
      return GetNBytes ();
    }
  else if (GetMode () == QUEUE_DISC_MODE_PACKETS)
    {
      return GetNPackets ();
    }
  else
    {
      NS_ABORT_MSG ("Unknown HHF mode.");
    }
}

void
HhfQueueDisc::SetQuantum (uint32_t quantum)
{
  NS_LOG_FUNCTION (this << quantum);
  m_quantum = quantum;
}

uint32_t
HhfQueueDisc::GetQuantum (void) const
{
  return m_quantum;
}

HhfQueueDisc::ListHead::ListHead ()
{
  NS_LOG_FUNCTION (this);
  prev = this;
  next = this;
  //NS_LOG_LOGIC ("ListHead Constructor");
}

HhfQueueDisc::ListHead::~ListHead ()
{
  NS_LOG_FUNCTION (this);
}

bool
HhfQueueDisc::ListHead::ListEmpty (void)
{
  NS_LOG_FUNCTION (this);
  return next == this;
}

void
HhfQueueDisc::ListHead::ListAddTail (ListHead* newHead)
{
  NS_LOG_FUNCTION (this);
  ListHead *prevHead = prev;
  ListHead *nextHead = this;

  nextHead->prev = newHead;
  newHead->next = nextHead;
  newHead->prev = prevHead;
  prevHead->next = newHead;
}

void
HhfQueueDisc::ListHead::ListMoveTail (ListHead* list)
{
  NS_LOG_FUNCTION (this);
  ListDelete (list);
  ListAddTail (list);
}

void
HhfQueueDisc::ListHead::ListDelete (ListHead *entry)
{
  NS_LOG_FUNCTION (this);
  ListHead *prevListHead = entry->prev;
  ListHead *nextListHead = entry->next;

  nextListHead->prev = prevListHead;
  prevListHead->next = nextListHead;
}

void
HhfQueueDisc::ListHead::InitializeListHead (ListHead *list)
{
  NS_LOG_FUNCTION (this);
  list->next = list;
  list->prev = list;
  NS_LOG_LOGIC ("Initialized Parameters");
}

int
HhfQueueDisc::ListHead::ListIsLast (ListHead *head)
{
  return next == head;
}

HhfQueueDisc::WdrrBucket *
HhfQueueDisc::ListFirstEntry (ListHead *list)
{
  NS_LOG_FUNCTION (this);
  ListHead *head = list->next;
  if (head == &m_buckets [WDRR_BUCKET_FOR_HH].bucketChain)
    {
      return &m_buckets [WDRR_BUCKET_FOR_HH];
    }
  else
    {
      return &m_buckets [WDRR_BUCKET_FOR_NON_HH];
    }
}

uint32_t
HhfQueueDisc::DoDrop (void) 
{
  NS_LOG_FUNCTION (this);
  WdrrBucket *bucket = &m_buckets [WDRR_BUCKET_FOR_HH];
  if (bucket->bucketChain.ListEmpty ())
    {
      bucket = &m_buckets [WDRR_BUCKET_FOR_NON_HH];
    }
  if (!bucket->bucketChain.ListEmpty ())
    {
      Ptr<const QueueDiscItem> item = bucket->packetQueue->Peek ();
      bucket->packetQueue->Dequeue ();
      Drop (item);
    }
  NS_LOG_LOGIC ("Finished DoDrop");
  return bucket - m_buckets;
}

HhfQueueDisc::FlowState *
HhfQueueDisc::SeekList (uint32_t hash, ListHead *head)
{
  FlowState *flow = 0;
  Time now = Simulator::Now ();
  NS_LOG_LOGIC ("Time at present inside SeekList" << now);
  if (head->ListEmpty ())
    {
      NS_LOG_LOGIC ("Finished Seek1");
      return 0;
    }
  for (ListHead *index = head->next; index != head; index = index->next)
    {
      for (std::list<FlowState*>::iterator it = m_tmpArray.begin (); it != m_tmpArray.end (); it++)
        {
          flow = *it;
          if (&flow->flowChain == index)
            {
              Time previous = flow->hitTimeStamp + m_evictTimeout;

              if ((previous - now) < 0)
                {
                  if (flow->flowChain.ListIsLast (head))
                    {
                      NS_LOG_LOGIC ("Finished Seek2");
                      return 0;
                    }
                  flow->flowChain.ListDelete (&flow->flowChain);
                  delete flow;
                  m_tmpArray.erase (it);
                  m_hhFlowsCurrentCnt--;
                }
              else if (flow->hashId == hash)
                {
                  NS_LOG_LOGIC ("Finished Seek3");
                  return flow;
                }
            }
        }
      }
   NS_LOG_LOGIC ("Finished Seek4");
  return 0;
}

HhfQueueDisc::FlowState *
HhfQueueDisc::AllocNewHh (ListHead *head)
{
  FlowState *flow = 0;
  Time now = Simulator::Now ();
  NS_LOG_LOGIC ("Time at present inside AllocNewHh" << now);
  if (!head->ListEmpty ())
    {
      //find an expired heavy-hitter flow entry.
      for (ListHead *index = head->next; index != head; index = index->next)
        {
          for (std::list<FlowState*>::iterator it = m_tmpArray.begin (); it != m_tmpArray.end (); it++)
            {
              flow = *it;
              if (&flow->flowChain == index)
                {
                  Time previous = flow->hitTimeStamp + m_evictTimeout;
                  if ((previous - now) < 0)
                    {
                      NS_LOG_LOGIC ("Finished Alloc1");
                      return flow;
                    }
                }
            }
        }
    }
  if (m_hhFlowsCurrentCnt >= m_hhFlowsLimit)
    {
      m_hhFlowsOverLimit++;
       NS_LOG_LOGIC ("Finished Alloc2");
      return 0;
    }
  flow = new FlowState;
  if (!flow)
    {
      NS_LOG_LOGIC ("Finished Alloc3");
      return 0;
    }
  m_hhFlowsCurrentCnt++;
  flow->flowChain.InitializeListHead (&flow->flowChain);
  head->ListAddTail (&flow->flowChain);
  m_tmpArray.push_back (flow);

   NS_LOG_LOGIC ("Finished Alloc4");
  return flow;
}

HhfQueueDisc::WdrrBucketIndex
HhfQueueDisc::DoClassify (Ptr<QueueDiscItem> item)
{
  uint32_t tmpHash = 0, hash = 0;
  uint32_t xorsum = 0, filterPosition [HHF_ARRAYS_CNT], flowPosition = 0;
  FlowState *flow = 0;
  uint32_t pktLen = 0, minHhfVal = 0;
  Time previous;
  Time now = Simulator::Now ();
  NS_LOG_LOGIC ("Initial Flow Pointer is " << flow);
  //rest the HHF counter arrays if this is the right time.
  previous = m_arraysResetTimestamp + m_resetTimeout;
  NS_LOG_LOGIC ("The time Previous = " << previous);
  NS_LOG_LOGIC ("The time Now = " << now);
  if ((previous - now) < 0)
    {
      for (int i = 0; i < HHF_ARRAYS_CNT; i++)
        {
          m_validBits [i] = 0;
        }
      m_arraysResetTimestamp = now;
      NS_LOG_LOGIC ("reset valid bits");
    }

  //get hashed flow-id of the packet.
  hash = Classify (item);
  NS_LOG_LOGIC ("Hashed Value from Packet Filter " << hash);

  //check if this packet belongs to an already established HH flow.
  flowPosition = hash & HHF_BIT_MASK;
  NS_LOG_LOGIC ("The flowPosition is " << flowPosition);
  flow = SeekList (hash, m_hhFlows [flowPosition]);
  NS_LOG_LOGIC ("Later Flow Pointer is " << flow);
  if (flow)
    {
      flow->hitTimeStamp = now;
       NS_LOG_LOGIC ("Finished Classify1");
      return WDRR_BUCKET_FOR_HH;
    }

  //now pass the packet through the multi-stage filter.
  tmpHash = hash;
  xorsum = 0;
  for (int i = 0; i < HHF_ARRAYS_CNT - 1; i++)
    {
      //Split the skb_hash into three 10-bit chunks. 
      filterPosition [i] = tmpHash & HHF_BIT_MASK;
      xorsum ^= filterPosition [i];
      tmpHash >>= HHF_BIT_MASK_LEN;
      NS_LOG_LOGIC ("The filterPosition is " << filterPosition [i]);
    }

  //the last chunk is computed as XOR sum of other chunks.
  filterPosition [HHF_ARRAYS_CNT - 1] = xorsum ^ tmpHash;
  NS_LOG_LOGIC ("The filterPosition is " << filterPosition [3]);

  pktLen = item->GetSize ();
  NS_LOG_LOGIC ("The packet length is " << pktLen);
  minHhfVal = ~0;
  for (int i = 0; i < HHF_ARRAYS_CNT; i++)
    {
      uint32_t val = 0;
      uint32_t bitmask = (1 << (filterPosition [i] >> 5));
      NS_LOG_LOGIC ("The bitmask is " << bitmask);
      NS_LOG_LOGIC (*m_validBits [i]);
      if (!((*m_validBits [i] & bitmask) != 0))
        {
          m_counterArrays [i][filterPosition[i]] = 0;
          *m_validBits [i] |= bitmask;
          NS_LOG_LOGIC ("which means the m_validBits [i] was zero");
        }
      NS_LOG_LOGIC (*m_validBits [i]);
      val = m_counterArrays [i][filterPosition[i]] + pktLen;
      NS_LOG_LOGIC (m_counterArrays [i]);
      NS_LOG_LOGIC ("The val is" << val);

      if (minHhfVal > val)
        {
          minHhfVal = val;
          NS_LOG_LOGIC ("the minimum value " << minHhfVal);
        } 
    }

  //found a new HH iff all counter values > HHF admit threshold.
  if (minHhfVal > m_admitBytes)
    {
      flow = AllocNewHh (m_hhFlows [flowPosition]);
      NS_LOG_LOGIC ("The flow here is " << flow);
      if (!flow)
        {
          NS_LOG_LOGIC ("Finished Classify2");
          return WDRR_BUCKET_FOR_NON_HH;
        }
      flow->hashId = hash;
      flow->hitTimeStamp = now;
      m_hhFlowsTotalCnt++;

      NS_LOG_LOGIC ("Finished Classify3");
      return WDRR_BUCKET_FOR_HH;
    }

  //conservative update of HHF arrays.
  for (int i = 0; i < HHF_ARRAYS_CNT; i++)
    {
      if (m_counterArrays [i][filterPosition [i]] < minHhfVal)
        {
          m_counterArrays [i][filterPosition [i]] = minHhfVal;
          NS_LOG_LOGIC ("minHhfVal " << minHhfVal);
          NS_LOG_LOGIC ("COUNTER arrays " << m_counterArrays [i][filterPosition [i]]);
        }
    }
  NS_LOG_LOGIC ("Finished Classify4");
  return WDRR_BUCKET_FOR_NON_HH;
}

bool
HhfQueueDisc::DoEnqueue (Ptr<QueueDiscItem> item)
{
  NS_LOG_FUNCTION (this << item);
  NS_LOG_LOGIC ("Entered DoEnqueue");
  WdrrBucketIndex idx;// = WDRR_BUCKET_FOR_NON_HH;
  WdrrBucket *bucket;// = new WdrrBucket;
  NS_LOG_LOGIC ("Packet Size" << item->GetSize ());
  idx = DoClassify (item);

  if (!idx)
    {
      NS_LOG_ERROR ("No filter has been able to classify this packet, drop it.");
      Drop (item);
      NS_LOG_LOGIC ("Finished Enqueue1");
      return false;
    }

  bucket = &m_buckets [idx];
  bucket->packetQueue->Enqueue (item);

  if (bucket->bucketChain.ListEmpty ())
    {
      uint32_t weight;
      if (idx == WDRR_BUCKET_FOR_HH)
        {
          weight = 1;
          m_oldBuckets.ListAddTail (&bucket->bucketChain);
        }
      else
        {
          weight = m_nonHhWeight;
          m_newBuckets.ListAddTail (&bucket->bucketChain);
        }
      bucket->deficit = weight * m_quantum;
    }
  if (GetDiscSize () <= m_limit)
    {
      NS_LOG_LOGIC ("Finished Enqueue2");
      return true;
    }

  m_dropOverLimit++;

  if (DoDrop () == idx)
    {
      NS_LOG_LOGIC ("Finished Enqueue3");
      return true; // linux code returns a congestion limit flag.
    }
  NS_LOG_LOGIC ("Finished Enqueue4");
  return true;
}

Ptr<QueueDiscItem>
HhfQueueDisc::DoDequeue (void)
{
  NS_LOG_FUNCTION (this);

  Ptr<QueueDiscItem> item = 0;
  WdrrBucket *bucket = new WdrrBucket;
  ListHead *head = new ListHead;
  
  do
    {
      do
        {
          head = &m_newBuckets;
          if (head->ListEmpty ())
            {
              head = &m_oldBuckets;
              if (head->ListEmpty ())
                {
                  NS_LOG_LOGIC ("Finished Dequeue1");
                  return NULL;
                }
            }

          bucket = ListFirstEntry (head); //returns the pointer to the bucket containing the head node of the list.
          NS_LOG_LOGIC ("bucket " << bucket);
          if (bucket->deficit <= 0)
            {
              int weight = ((bucket - m_buckets) == WDRR_BUCKET_FOR_HH) ? 1 : m_nonHhWeight;
              NS_LOG_LOGIC ("The weight is " << weight);
              bucket->deficit += (weight * m_quantum);
              m_oldBuckets.ListMoveTail (&bucket->bucketChain);
            }
          NS_LOG_LOGIC ("The deficit before dequeuing" << bucket->deficit);
        } while (bucket->deficit <= 0);

        if (!bucket->bucketChain.ListEmpty ())
          {
            item = bucket->packetQueue->Dequeue ();
            NS_LOG_LOGIC ("item extracted" << item);
          }
        if (!item)
          {
            if ((head == &m_newBuckets) && (!m_oldBuckets.ListEmpty ()))
              {
                m_oldBuckets.ListMoveTail (&bucket->bucketChain);
              }
            else
              {
                bucket->bucketChain.ListDelete (&bucket->bucketChain);
                bucket->bucketChain.InitializeListHead (&bucket->bucketChain);
              }
          }
     } while (!item);

  bucket->deficit -= item->GetSize ();
  NS_LOG_LOGIC ("The deficit after dequeuing" << bucket->deficit);
  NS_LOG_LOGIC ("Finished Dequeue2");
  return item;
}

Ptr<const QueueDiscItem>
HhfQueueDisc::DoPeek (void) const
{
  NS_LOG_FUNCTION (this);
  Ptr<const QueueDiscItem> item = 0;//Peek ();

  //NS_LOG_LOGIC ("Number packets " << GetNPackets ());
  //NS_LOG_LOGIC ("Number bytes " << GetNBytes ());

  return item;
}

bool
HhfQueueDisc::CheckConfig (void)
{
  NS_LOG_FUNCTION (this);
  if (GetNQueueDiscClasses () > 0)
    {
      NS_LOG_ERROR ("HhfQueueDisc cannot have classes");
      return false;
    }

  Ptr<UniformRandomVariable> rand = CreateObject<UniformRandomVariable> ();
  Ptr<FqCoDelIpv4PacketFilter> filterIpv4 = CreateObjectWithAttributes<FqCoDelIpv4PacketFilter> ("Perturbation", UintegerValue (rand->GetInteger ()));
  Ptr<FqCoDelIpv6PacketFilter> filterIpv6 = CreateObjectWithAttributes<FqCoDelIpv6PacketFilter> ("Perturbation", UintegerValue (rand->GetInteger ()));
  AddPacketFilter (filterIpv4);
  AddPacketFilter (filterIpv6);

  if (GetNPacketFilters () == 0)
    {
      NS_LOG_ERROR ("HhfQueueDisc needs at least one packet filter");
      return false;
    }

  if (GetNInternalQueues () > 0)
    {
      NS_LOG_ERROR ("HhfQueueDisc cannot have internal queues at this stage - they are about to be created");
      return false;
    }


    // create two DropTail queues
    Ptr<InternalQueue> queueHH = CreateObjectWithAttributes<DropTailQueue<QueueDiscItem> > ("Mode", EnumValue (m_mode));
    Ptr<InternalQueue> queueNonHH = CreateObjectWithAttributes<DropTailQueue<QueueDiscItem> > ("Mode", EnumValue (m_mode));
    
    if (m_mode == QUEUE_DISC_MODE_PACKETS)
      {
        queueHH->SetMaxPackets (m_limit);
        queueNonHH->SetMaxPackets (m_limit);
      }
    else
      {
        queueHH->SetMaxBytes (m_limit);
        queueNonHH->SetMaxBytes (m_limit);
      }

    AddInternalQueue (queueHH);
    AddInternalQueue (queueNonHH);
    m_buckets [WDRR_BUCKET_FOR_NON_HH].packetQueue = queueNonHH;
    m_buckets [WDRR_BUCKET_FOR_HH].packetQueue = queueHH;

  if (GetNInternalQueues () != 2)
    {
      NS_LOG_ERROR ("HhfQueueDisc needs 2 internal queues");
      return false;
    }
  NS_LOG_LOGIC ("HhfQueueDisc CheckConfig done");
  return true;
}

void
HhfQueueDisc::InitializeParams (void)
{
  NS_LOG_FUNCTION (this);

  // we are at initialization time. If the user has not set a quantum value,
  // set the quantum to the MTU of the device
  if (!m_quantum)
    {
      Ptr<NetDevice> device = GetNetDevice ();
      NS_ASSERT_MSG (device, "Device not set for the queue disc");
      m_quantum = device->GetMtu ();
      NS_LOG_DEBUG ("Setting the quantum to the MTU of the device: " << m_quantum);
    }
  m_newBuckets.InitializeListHead (&m_newBuckets);
  m_oldBuckets.InitializeListHead (&m_oldBuckets);

  if (!m_hhFlows [HH_FLOW_CNT])
    {
      for (int i = 0; i < HH_FLOW_CNT; i++)
        {
          m_hhFlows[i] = new ListHead ();
          if (!m_hhFlows [i])
            {
              NS_LOG_ERROR ("Could not create the Flow Table T");
            }
        }

      // Cap max active HHs at twice the length of Flow Table T.
      m_hhFlowsLimit = 2 * HH_FLOW_CNT;
      m_hhFlowsOverLimit = 0;
      m_hhFlowsTotalCnt = 0;
      m_hhFlowsCurrentCnt = 0;

      // Initialize the heavy hitter filter arrays 
      for (int i = 0; i < HHF_ARRAYS_CNT; i++)
        {
          m_counterArrays[i] = new uint32_t;
          if (!m_counterArrays [i])
            {
              NS_LOG_ERROR ("Could not create the Counter Arrays");
            }
        }
      m_arraysResetTimestamp = Simulator::Now ();

      for (int i = 0; i < HHF_ARRAYS_CNT; i++)
        {
          m_validBits[i] = new uint32_t;
          if (!m_validBits [i])
            {
              NS_LOG_ERROR ("Could not create the ValidBits pointer array");
            }
        }

      for (int i = 0; i < WDRR_BUCKET_CNT; i++)
        {
          WdrrBucket *bucket = m_buckets + i;
          bucket->bucketChain.InitializeListHead (&bucket->bucketChain);
        }
    }
  m_tmpArray = {};
  NS_LOG_DEBUG ("InitializeParams done");
}

} // namespace ns3
