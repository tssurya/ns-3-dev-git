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
 * TBF, The Token Bucket Filter Queueing discipline
 * 
 * This implementation is based on linux kernel code by
 * Authors:     Alexey Kuznetsov, <kuznet@ms2.inr.ac.ru>
 *              Dmitry Torokhov <dtor@mail.ru> - allow attaching inner qdiscs -
 *                                               original idea by Martin Devera
 *
 * Implemented in ns-3 by: Surya Seetharaman <suryaseetharaman.9@gmail.com>
 *                         Stefano Avallone <stavallo@unina.it>
 */

#include "ns3/log.h"
#include "ns3/enum.h"
#include "ns3/simulator.h"
#include "ns3/uinteger.h"
#include "ns3/attribute.h"
#include "ns3/object-factory.h"
#include "ns3/drop-tail-queue.h"
#include "ns3/net-device-queue-interface.h"
#include "tbf-queue-disc.h"

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("TbfQueueDisc");

NS_OBJECT_ENSURE_REGISTERED (TbfQueueDisc);

TypeId TbfQueueDisc::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::TbfQueueDisc")
    .SetParent<QueueDisc> ()
    .SetGroupName ("TrafficControl")
    .AddConstructor<TbfQueueDisc> ()
    .AddAttribute ("Mode",
                   "Determines unit for QueueLimit",
                   EnumValue (QUEUE_DISC_MODE_BYTES),
                   MakeEnumAccessor (&TbfQueueDisc::SetMode),
                   MakeEnumChecker (QUEUE_DISC_MODE_BYTES, "QUEUE_DISC_MODE_BYTES",
                                    QUEUE_DISC_MODE_PACKETS, "QUEUE_DISC_MODE_PACKETS"))
    .AddAttribute ("QueueLimit",
                   "Queue limit in bytes/packets",
                   UintegerValue (125000),
                   MakeUintegerAccessor (&TbfQueueDisc::SetQueueLimit),
                   MakeUintegerChecker<uint32_t> ())
    .AddAttribute ("Burst",
                   "Size of the first bucket in bytes",
                   UintegerValue (125000),
                   MakeUintegerAccessor (&TbfQueueDisc::SetBurst),
                   MakeUintegerChecker<uint32_t> ())
    .AddAttribute ("Mtu",
                   "Size of the second bucket in bytes",
                   UintegerValue (0),
                   MakeUintegerAccessor (&TbfQueueDisc::SetMtu),
                   MakeUintegerChecker<uint32_t> ())
    .AddAttribute ("Rate",
                   "Speed at which tokens enter the first bucket in bps or Bps.",
                   DataRateValue (DataRate ("125KB/s")),
                   MakeDataRateAccessor (&TbfQueueDisc::SetRate),
                   MakeDataRateChecker ())
    .AddAttribute ("PeakRate",
                   "Speed at which tokens enter the second bucket in bps or Bps.",
                   DataRateValue (DataRate ("0KB/s")),
                   MakeDataRateAccessor (&TbfQueueDisc::SetPeakRate),
                   MakeDataRateChecker ())
    .AddTraceSource ("TokensInFirstBucket",
                     "Number of First Bucket Tokens in bytes",
                     MakeTraceSourceAccessor (&TbfQueueDisc::m_btokens),
                     "ns3::TracedValueCallback::Uint32")
    .AddTraceSource ("TokensInSecondBucket",
                     "Number of Second Bucket Tokens in bytes",
                     MakeTraceSourceAccessor (&TbfQueueDisc::m_ptokens),
                     "ns3::TracedValueCallback::Uint32")
  ;

  return tid;
}

TbfQueueDisc::TbfQueueDisc () :
  QueueDisc ()
{
  NS_LOG_FUNCTION (this);
}

TbfQueueDisc::~TbfQueueDisc ()
{
  NS_LOG_FUNCTION (this);
}

void
TbfQueueDisc::DoDispose (void)
{
  NS_LOG_FUNCTION (this);
  QueueDisc::DoDispose ();
}

void
TbfQueueDisc::SetMode (QueueDiscMode mode)
{
  NS_LOG_FUNCTION (this << mode);
  m_mode = mode;
}

TbfQueueDisc::QueueDiscMode
TbfQueueDisc::GetMode (void)
{
  NS_LOG_FUNCTION (this);
  return m_mode;
}

void
TbfQueueDisc::SetQueueLimit (uint32_t lim)
{
  NS_LOG_FUNCTION (this << lim);
  m_limit = lim;
}

void
TbfQueueDisc::SetBurst (uint32_t burst)
{
  NS_LOG_FUNCTION (this << burst);
  m_burst = burst;
}

uint32_t
TbfQueueDisc::GetBurst (void) const
{
  NS_LOG_FUNCTION (this);
  return m_burst;
}

void
TbfQueueDisc::SetMtu (uint32_t mtu)
{
  NS_LOG_FUNCTION (this << mtu);
  m_mtu = mtu;
}

uint32_t
TbfQueueDisc::GetMtu (void) const
{
  NS_LOG_FUNCTION (this);
  return m_mtu;
}

void
TbfQueueDisc::SetRate (DataRate rate)
{
  NS_LOG_FUNCTION (this << rate);
  m_rate = rate;
}

DataRate
TbfQueueDisc::GetRate (void) const
{
  NS_LOG_FUNCTION (this);
  return m_rate;
}

void
TbfQueueDisc::SetPeakRate (DataRate peakRate)
{
  NS_LOG_FUNCTION (this << peakRate);
  m_peakRate = peakRate;
}

DataRate
TbfQueueDisc::GetPeakRate (void) const
{
  NS_LOG_FUNCTION (this);
  return m_peakRate;
}

uint32_t
TbfQueueDisc::GetFirstBucketTokens (void) const
{
  NS_LOG_FUNCTION (this);
  return m_btokens;
}

uint32_t
TbfQueueDisc::GetSecondBucketTokens (void) const
{
  NS_LOG_FUNCTION (this);
  return m_ptokens;
}

uint32_t
TbfQueueDisc::GetQueueSize (void)
{
  NS_LOG_FUNCTION (this);
  if (GetMode () == QUEUE_DISC_MODE_BYTES)
    {
      return GetInternalQueue (0)->GetNBytes ();
    }
  else if (GetMode () == QUEUE_DISC_MODE_PACKETS)
    {
      return GetInternalQueue (0)->GetNPackets ();
    }
  else
    {
      NS_ABORT_MSG ("Unknown TBF mode.");
    }
}

bool
TbfQueueDisc::DoEnqueue (Ptr<QueueDiscItem> item)
{
  NS_LOG_FUNCTION (this << item);

  bool retval = GetInternalQueue (0)->Enqueue (item);

  // If Queue::Enqueue fails, QueueDisc::Drop is called by the internal queue
  // because QueueDisc::AddInternalQueue sets the drop callback

  NS_LOG_LOGIC ("Number packets " << GetInternalQueue (0)->GetNPackets ());
  NS_LOG_LOGIC ("Number bytes " << GetInternalQueue (0)->GetNBytes ());

  return retval;
}

Ptr<const QueueDiscItem>
TbfQueueDisc::DoPeek () const
{
  NS_LOG_FUNCTION (this);
  if (GetInternalQueue (0)->IsEmpty ())
    {
      NS_LOG_LOGIC ("Queue empty");
      return 0;
    }

  Ptr<const QueueDiscItem> item = GetInternalQueue (0)->Peek ();

  NS_LOG_LOGIC ("Number packets " << GetInternalQueue (0)->GetNPackets ());
  NS_LOG_LOGIC ("Number bytes " << GetInternalQueue (0)->GetNBytes ());

  return item;
}

Ptr<QueueDiscItem>
TbfQueueDisc::DoDequeue (void)
{
  NS_LOG_FUNCTION (this);
  Ptr<QueueDiscItem> item = 0;
  Ptr<const QueueDiscItem> itemPeek = DoPeek ();

  if (itemPeek)
    {
      int64_t packetByteSize = itemPeek->GetSize ();
      NS_LOG_LOGIC ("Packet Size " << packetByteSize);
      
      int64_t btoks = 0;
      int64_t ptoks = 0;
      Time now = Simulator::Now ();
      
      double delta = (now  - m_timeCheckPoint).GetSeconds ();
      NS_LOG_LOGIC ("Time Difference delta " << delta);

      if (m_mtu > 0 && m_peakRate > DataRate ("0bps"))
        {
          ptoks =  m_ptokens + round (delta * (m_peakRate.GetBitRate () / 8));
          if (ptoks > m_mtu)
            {
              ptoks = m_mtu;
            }
          NS_LOG_LOGIC ("ptoks " << ptoks);
          ptoks -= packetByteSize;
          NS_LOG_LOGIC ("ptoks " << ptoks);
        }

      btoks = m_btokens + round (delta * (m_rate.GetBitRate () / 8));

      if (btoks > m_burst)
        {
          btoks = m_burst;
        }

      NS_LOG_LOGIC ("btoks " << btoks);
      btoks -= packetByteSize;
      NS_LOG_LOGIC ("btoks " << btoks);

      if ((btoks|ptoks) >= 0) // else packet blocked
        {
          item = GetInternalQueue (0)->Dequeue ();
          if (!item)
            {
              return item;
            }

          m_timeCheckPoint = now;
          m_btokens = btoks;
          m_ptokens = ptoks;

          NS_LOG_LOGIC ("Number packets " << GetInternalQueue (0)->GetNPackets ());
          NS_LOG_LOGIC ("Number bytes " << GetInternalQueue (0)->GetNBytes ());

        }
      // the watchdog timer setup.
      /*A packet gets blocked if the above if condition is not satisfied, i.e 
      both the ptoks and btoks are less than zero. In that case we have to 
      schedule the waking of queue when enough tokens are available.*/
      else
        {
          if (m_id.IsExpired () == true)
            {
              int64_t requiredTokens = 0;
              Time requiredDelayTime = Seconds (0);
              if(-btoks > -ptoks)
                {
                  requiredTokens = -btoks;
                  requiredDelayTime = m_rate.CalculateBytesTxTime (requiredTokens);
                }
              else
                {
                  requiredTokens = -ptoks;
                  requiredDelayTime = m_peakRate.CalculateBytesTxTime (requiredTokens);
                }

              NS_LOG_LOGIC("Packet Blocked: Number of required tokens are " << requiredTokens);
              m_id = Simulator::Schedule (requiredDelayTime, &QueueDisc::Run, this);
              NS_LOG_LOGIC("Waking Event Scheduled in " << requiredDelayTime);
            }
        }
    }
  return item;
}

bool
TbfQueueDisc::CheckConfig (void)
{
  NS_LOG_FUNCTION (this);
  if (GetNQueueDiscClasses () > 0)
    {
      NS_LOG_ERROR ("TbfQueueDisc cannot have classes");
      return false;
    }

  if (GetNPacketFilters () > 0)
    {
      NS_LOG_ERROR ("TbfQueueDisc cannot have packet filters");
      return false;
    }

  if (GetNInternalQueues () == 0)
    {
      // create a DropTail queue
      Ptr<InternalQueue> queue = CreateObjectWithAttributes<DropTailQueue<QueueDiscItem> > ("Mode", EnumValue (m_mode));
      if (m_mode == QUEUE_DISC_MODE_PACKETS)
        {
          queue->SetMaxPackets (m_limit);
        }
      else
        {
          queue->SetMaxBytes (m_limit);
        }
      AddInternalQueue (queue);
    }

  if (GetNInternalQueues () != 1)
    {
      NS_LOG_ERROR ("TbfQueueDisc needs 1 internal queue");
      return false;
    }


  if ((GetInternalQueue (0)->GetMode () == QueueBase::QUEUE_MODE_PACKETS && m_mode == QUEUE_DISC_MODE_BYTES) ||
      (GetInternalQueue (0)->GetMode () == QueueBase::QUEUE_MODE_BYTES && m_mode == QUEUE_DISC_MODE_PACKETS))
    {
      NS_LOG_ERROR ("The mode of the provided queue does not match the mode set on the TbfQueueDisc");
      return false;
    }

  if ((m_mode ==  QUEUE_DISC_MODE_PACKETS && GetInternalQueue (0)->GetMaxPackets () < m_limit) ||
      (m_mode ==  QUEUE_DISC_MODE_BYTES && GetInternalQueue (0)->GetMaxBytes () < m_limit))
    {
      NS_LOG_ERROR ("The size of the internal queue is less than the queue disc limit");
      return false;
    }

  if (m_mtu == 0 and GetNetDevice () and m_peakRate > DataRate ("0bps"))
    {
      m_mtu = GetNetDevice ()->GetMtu ();
    }

  if (m_mtu == 0 and m_peakRate > DataRate ("0bps"))
    {
      NS_LOG_ERROR ("The size of the second bucket is not set though the peakRate is set");
      return false;
    }

  if (m_mtu != 0 and m_peakRate == DataRate ("0bps"))
    {
      NS_LOG_ERROR ("The peakRate is not set though the size of the second bucket is set");
      return false;
    }

  if (m_burst <= m_mtu)
    {
      NS_LOG_LOGIC ("m_burst and m_mtu " << m_burst << m_mtu);
      NS_LOG_ERROR ("The size of the first bucket should be greater than the size of the second bucket.");
      return false;
    }

  if (m_peakRate > DataRate ("0bps") && m_peakRate <= m_rate)
    {
      NS_LOG_ERROR ("The token rate for second bucket should be greater than the token rate for first bucket for burst condition to be handled.");
      return false;
    }

  return true;
}

void
TbfQueueDisc::InitializeParams (void)
{
  NS_LOG_FUNCTION (this);
  // Token Buckets are full at the beginning.
  m_btokens = m_burst;
  m_ptokens = m_mtu;
  // Initialising other variables to 0.
  m_timeCheckPoint = Seconds (0);
  m_id = EventId ();
}

} // namespace ns3
