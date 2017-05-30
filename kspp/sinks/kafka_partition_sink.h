#include <assert.h>
#include <memory>
#include <functional>
#include <sstream>
#include <kspp/kspp.h>
#include <kspp/impl/sinks/kafka_producer.h>
#pragma once

namespace kspp {
  // SINGLE PARTITION PRODUCER
  // this is just to only override the necessary key value specifications
template<class K, class V, class CODEC>
class kafka_partition_sink_base : public partition_sink<K, V>
{
  protected:
  kafka_partition_sink_base(std::string brokers, std::string topic, int32_t partition, std::chrono::milliseconds max_buffering_time, std::shared_ptr<CODEC> codec)
    : partition_sink<K, V>(partition)
    , _codec(codec)
    , _impl(brokers, topic, max_buffering_time)
    , _fixed_partition(partition)
    , _in_count("in_count")
    , _lag() {
    this->add_metric(&_in_count);
    this->add_metric(&_lag);
  }
   
  virtual std::string name() const {
    return "kafka_partition_sink(" + _impl.topic() + "#" + std::to_string(_fixed_partition) + ")-codec(" + CODEC::name() + ")[" + type_name<K>::get() + ", " + type_name<V>::get() + "]";
  }

  virtual std::string processor_name() const { return "kafka_partition_sink(" + _impl.topic() + ")"; }

  virtual void close() {
    flush();
    return _impl.close();
  }

  virtual size_t queue_len() {
    return this->_queue.size() + _impl.queue_len();
  }

  virtual void poll(int timeout) {
    return _impl.poll(timeout);
  }

  virtual void commit(bool flush) {
    // noop
  }

  virtual void flush() {
    while (!eof()) {
      process_one(kspp::milliseconds_since_epoch());
      poll(0);
    }

    while (true) {
      auto ec = _impl.flush(1000);
      if (ec == 0)
        break;
    }
  }

  virtual bool eof() const {
    return this->_queue.size() == 0;
  }

  protected:
  kafka_producer          _impl;
  std::shared_ptr<CODEC>  _codec;
  size_t                  _fixed_partition;
  metric_counter          _in_count;
  metric_lag              _lag;
};

template<class K, class V, class CODEC>
class kafka_partition_sink : public kafka_partition_sink_base<K, V, CODEC>
{
  public:
  kafka_partition_sink(topology_base& topology, std::string topic, std::shared_ptr<CODEC> codec = std::make_shared<CODEC>())
    : kafka_partition_sink_base<K, V, CODEC>(topology.brokers(), topic, topology.partition(), topology.max_buffering_time(), codec) {
  }

  virtual ~kafka_partition_sink() {
    close();
  }

  // lets try to get as much as possible from queue to librdkafka - stop when queue is empty or librdkafka fails
  virtual bool process_one(int64_t tick) {
    if (this->_queue.size() == 0)
      return false;
    size_t count = 0;

    while (this->_queue.size()) {
      auto ev = _queue.front();

      void* kp = nullptr;
      void* vp = nullptr;
      size_t ksize = 0;
      size_t vsize = 0;

      std::stringstream ks;
      ksize = this->_codec->encode(ev->record()->key, ks);
      kp = malloc(ksize);  // must match the free in kafka_producer TBD change to new[] and a memory pool
      ks.read((char*) kp, ksize);

      if (ev->record()->value) {
        std::stringstream vs;
        vsize = this->_codec->encode(*ev->record()->value, vs);
        vp = malloc(vsize);   // must match the free in kafka_producer TBD change to new[] and a memory pool
        vs.read((char*) vp, vsize);
      }
      if (_impl.produce((uint32_t) this->_fixed_partition, kafka_producer::FREE, kp, ksize, vp, vsize, ev->event_time(), ev->id()))
        return (count > 0);
      ++count;
      ++(this->_in_count);
      this->_lag.add_event_time(kspp::milliseconds_since_epoch(), ev->event_time()); // move outside loop
      this->_queue.pop_front();
    }
    return true;
  }

  protected:
  //  virtual int _produce(std::shared_ptr<kevent<K, V>> ev) {
  //    void* kp = nullptr;
  //    void* vp = nullptr;
  //    size_t ksize = 0;
  //    size_t vsize = 0;

  //    std::stringstream ks;
  //    ksize = this->_codec->encode(ev->record()->key, ks);
  //    kp = malloc(ksize);  // must match the free in kafka_producer TBD change to new[] and a memory pool
  //    ks.read((char*)kp, ksize);

  //    if (ev->record()->value) {
  //      std::stringstream vs;
  //      vsize = this->_codec->encode(*ev->record()->value, vs);
  //      vp = malloc(vsize);  // must match the free in kafka_producer TBD change to new[] and a memory pool
  //      vs.read((char*)vp, vsize);
  //    }
  //    ++(this->_in_count);
  //    this->_lag.add_event_time(kspp::milliseconds_since_epoch(), ev->event_time());
  //    return this->_impl.produce((uint32_t) this->_fixed_partition, kafka_producer::FREE, kp, ksize, vp, vsize, ev->event_time(), ev->id());
  //  }
};

// value only topic
template<class V, class CODEC>
class kafka_partition_sink<void, V, CODEC> : public kafka_partition_sink_base<void, V, CODEC>
{
  public:
  kafka_partition_sink(topology_base& topology, std::string topic, std::shared_ptr<CODEC> codec = std::make_shared<CODEC>())
    : kafka_partition_sink_base<void, V, CODEC>(topology.brokers(), topic, topology.partition(), topology.max_buffering_time(), codec) {
  }

  virtual ~kafka_partition_sink() {
    close();
  }

  // lets try to get as much as possible from queue to librdkafka - stop when queue is empty or librdkafka fails
  virtual bool process_one(int64_t tick) {
    if (this->_queue.size() == 0)
      return false;

    size_t count = 0;

    while (this->_queue.size()) {
      auto ev = _queue.front();
      void* vp = nullptr;
      size_t vsize = 0;

      if (ev->record()->value) {
        std::stringstream vs;
        vsize = this->_codec->encode(*ev->record()->value, vs);
        vp = malloc(vsize);   // must match the free in kafka_producer TBD change to new[] and a memory pool
        vs.read((char*) vp, vsize);
      }
      if (_impl.produce((uint32_t) this->_fixed_partition, kafka_producer::FREE, nullptr, 0, vp, vsize, ev->event_time(), ev->id()))
        return (count > 0);
      ++count;
      ++(this->_in_count);
      this->_lag.add_event_time(kspp::milliseconds_since_epoch(), ev->event_time()); // move outside loop
      this->_queue.pop_front();
    }
    return true;
  }

  protected:
    //virtual int _produce(std::shared_ptr<kevent<void, V>> ev) {
    //  void* vp = nullptr;
    //  size_t vsize = 0;

    //  if (ev->record()->value) {
    //    std::stringstream vs;
    //    vsize = this->_codec->encode(*ev->record()->value, vs);
    //    vp = malloc(vsize);  // must match the free in kafka_producer TBD change to new[] and a memory pool
    //    vs.read((char*)vp, vsize);
    //  }
    //  ++(this->_in_count);
    //  this->_lag.add_event_time(kspp::milliseconds_since_epoch(), ev->event_time());
    //  return this->_impl.produce((uint32_t) this->_fixed_partition, kafka_producer::FREE, nullptr, 0, vp, vsize, ev->event_time(), ev->id());
    //}
};

// key only topic
template<class K, class CODEC>
class kafka_partition_sink<K, void, CODEC> : public kafka_partition_sink_base<K, void, CODEC>
{
  public:
  kafka_partition_sink(topology_base& topology, std::string topic, std::shared_ptr<CODEC> codec = std::make_shared<CODEC>())
    : kafka_partition_sink_base<K, void, CODEC>(topology.brokers(), topic, topology.partition(), topology.max_buffering_time(), codec) {
  }

  virtual ~kafka_partition_sink() {
    close();
  }

  // lets try to get as much as possible from queue to librdkafka - stop when queue is empty or librdkafka fails
  virtual bool process_one(int64_t tick) {
    if (this->_queue.size() == 0)
      return false;
    size_t count = 0;

    while (this->_queue.size()) {
      auto ev = _queue.front();

      void* kp = nullptr;
      //void* vp = nullptr;
      size_t ksize = 0;
      //size_t vsize = 0;

      std::stringstream ks;
      ksize = this->_codec->encode(ev->record()->key, ks);
      kp = malloc(ksize);  // must match the free in kafka_producer TBD change to new[] and a memory pool
      ks.read((char*) kp, ksize);

      /*
      if (ev->record()->value) {
      std::stringstream vs;
      vsize = this->_codec->encode(*ev->record()->value, vs);
      vp = malloc(vsize);   // must match the free in kafka_producer TBD change to new[] and a memory pool
      vs.read((char*) vp, vsize);
      }
      */

      if (_impl.produce((uint32_t) this->_fixed_partition, kafka_producer::FREE, kp, ksize, nullptr, 0, ev->event_time(), ev->id()))
        return (count > 0);
      ++count;
      ++(this->_in_count);
      this->_lag.add_event_time(kspp::milliseconds_since_epoch(), ev->event_time()); // move outside loop
      this->_queue.pop_front();
    }
    return true;
  }

  protected:
    //virtual int _produce(std::shared_ptr<kevent<K, void>> ev) {
    //  void* kp = nullptr;
    //  size_t ksize = 0;

    //  std::stringstream ks;
    //  ksize = this->_codec->encode(ev->record()->key, ks);
    //  kp = malloc(ksize);  // must match the free in kafka_producer TBD change to new[] and a memory pool
    //  ks.read((char*)kp, ksize);
    //  ++(this->_in_count);
    //  this->_lag.add_event_time(kspp::milliseconds_since_epoch(), ev->event_time());
    //  return this->_impl.produce((uint32_t) this->_fixed_partition, kafka_producer::FREE, kp, ksize, nullptr, 0, ev->event_time(), ev->id());
    //}
};
};

