#include <functional>
#include <boost/log/trivial.hpp>
#include <kspp/kspp.h>
#include <deque>
#pragma once

namespace kspp {
  template<class K, class SV, class RV>
  class transform_value : public partition_source<K, RV>
  {
  public:
    typedef std::function<void(std::shared_ptr<kevent<K, SV>> record, transform_value* self)> extractor; // maybee better to pass this and send() directrly

    transform_value(topology_base& topology, std::shared_ptr<partition_source<K, SV>> source, extractor f)
      : partition_source<K, RV>(source.get(), source->partition())
      , _source(source)
      , _extractor(f) {
      _source->add_sink([this](auto r) {
        _queue.push_back(r);
      });
      this->add_metric(&_lag);
    }

    ~transform_value() {
      close();
    }
 
    std::string name() const {
      return _source->name() + "-transform_value";
    }

    virtual std::string processor_name() const {
      return "transform_value";
    }

    virtual void start() {
      _source->start();
    }

    virtual void start(int64_t offset) {
      _source->start(offset);
    }

    virtual void close() {
      _source->close();
    }

    virtual bool process_one(int64_t tick) {
      _source->process_one(tick);
      bool processed = (_queue.size() > 0);
      while (_queue.size()) {
        auto trans = _queue.front();
        _lag.add_event_time(tick, trans->event_time());
        _queue.pop_front();
        _extractor(trans, this);
      }
      return processed;
    }

    void push_back(std::shared_ptr<kevent<K, RV>> r) {
      this->send_to_sinks(r);
    }

    virtual void commit(bool flush) {
      _source->commit(flush);
    }

    virtual bool eof() const {
      return _source->eof() && (_queue.size() == 0);
    }

    virtual size_t queue_len() {
      return _queue.size();
    }

  private:
    std::shared_ptr<partition_source<K, SV>> _source;
    extractor                                _extractor;
    event_queue<kevent<K, SV>>               _queue;
    metric_lag                               _lag;
  };


  template<class K, class V>
  class transform : public partition_source<K, V>
  {
    public:
    typedef std::function<void(std::shared_ptr<kevent<K, V>> record, transform* self)> extractor; // maybe better to pass this and send() directrly

    transform(topology_base& topology, std::shared_ptr<partition_source<K, V>> source, extractor f)
      : partition_source<K, V>(source.get(), source->partition())
      , _source(source)
      , _extractor(f) {
      _source->add_sink([this](auto r) {
        _queue.push_back(r);
      });
      this->add_metric(&_lag);
    }

    ~transform() {
      close();
    }

    std::string name() const {
      return _source->name() + "-transform";
    }

    virtual std::string processor_name() const {
      return "transform";
    }

    virtual void start() {
      _source->start();
    }

    virtual void start(int64_t offset) {
      _source->start(offset);
    }

    virtual void close() {
      _source->close();
    }

    virtual bool process_one(int64_t tick) {
      _source->process_one(tick);
      bool processed = (_queue.size() > 0);
      while (_queue.size()) {
        auto trans = _queue.front();
        _lag.add_event_time(tick, trans->event_time());
        _queue.pop_front();
        _currrent_id = trans->id(); // we capture this to have it in push_back callback
        _extractor(trans, this);
        _currrent_id.reset(); // must be freed otherwise we continue to hold the last ev
      }
      return processed;
    }

    /*
    void push_back(std::shared_ptr<kevent<K, V>> r) {
      this->send_to_sinks(r);
    }
    */

    /**
    * use from from extractor callback
    */
    inline void push_back(std::shared_ptr<krecord<K, V>> record) {
      this->send_to_sinks(std::make_shared<kevent<K, V>>(record, _currrent_id));
    }

    virtual void commit(bool flush) {
      _source->commit(flush);
    }

    virtual bool eof() const {
      return _source->eof() && (_queue.size() == 0);
    }

    virtual size_t queue_len() {
      return _queue.size();
    }

    private:
    std::shared_ptr<partition_source<K, V>>          _source;
    extractor                                        _extractor;
    std::shared_ptr<commit_chain::autocommit_marker> _currrent_id; // used to briefly hold the commit open during process one
    event_queue<kevent<K, V>>                        _queue;
    metric_lag                                       _lag;
  };
}

