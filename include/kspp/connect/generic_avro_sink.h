#include <memory>
#include <strstream>
#include <thread>
#include <glog/logging.h>
#include <kspp/kspp.h>
#include <kspp/topology.h>
#include <kspp/avro/avro_generic.h>
#include <kspp/connect/generic_producer.h>
#pragma once

namespace kspp {
  class generic_avro_sink : public topic_sink<void, kspp::GenericAvro> {
  public:
    generic_avro_sink(topology &t,
                      std::shared_ptr<generic_producer<void, kspp::GenericAvro>> impl,
                      std::shared_ptr<kspp::avro_schema_registry> schema_registry)
    : _impl(impl)
    , _schema_registry(schema_registry) {
    }

    virtual ~generic_avro_sink() {
      close();
    }

    void close() override {
      if (!_exit) {
        _exit = true;
      }
      _impl->close();
    }

    bool eof() const override {
      return this->_queue.size()==0 && _impl->eof();
    }

    size_t queue_size() const override {
      return this->_queue.size();
    }

    int64_t next_event_time() const override {
      return this->_queue.next_event_time();
    }

    size_t process(int64_t tick) override {
      if (this->_queue.empty())
        return 0;
      size_t processed=0;
      while(!this->_queue.empty()) {
        auto p = this->_queue.front();
        if (p==nullptr || p->event_time() > tick)
          return processed;
        this->_queue.pop_front();
        _impl->insert(p);
        ++(this->_processed_count);
        ++processed;
        this->_lag.add_event_time(tick, p->event_time());
      }
      return processed;
    }

    std::string topic() const override {
      return _impl->topic();
    }

    void poll(int timeout) override {
      _impl->poll();
    }

    void flush() override {
      while (!eof()) {
        process(kspp::milliseconds_since_epoch());
        poll(0);
        std::this_thread::sleep_for(std::chrono::milliseconds(10)); // TODO the deletable messages should be deleted when poill gets called an not from background thread 3rd queue is needed...
      }

      while (true) {
        int ec=0; // TODO fixme
        //auto ec = _impl.flush(1000);
        if (ec == 0)
          break;
      }
    }

  protected:
    bool _started;
    bool _exit;
    std::shared_ptr<generic_producer<void, kspp::GenericAvro>> _impl;
    std::shared_ptr<kspp::avro_schema_registry> _schema_registry;
    //std::shared_ptr<avro::ValidSchema> _schema;
    //int32_t _schema_id;
  };
}

