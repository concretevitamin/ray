// NOTE(zongheng): this sketches what I imagine would be a sensible interface.
//
// Notably missing:
//
// + The retry mechanism on timeout.  Since each holder of a Table is a
//   single-threaded event loop, it might be sensible for the table-holder to
//   set a timer to retry.
//
// + GcsHandle::AttachToEventLoop() is incorrect.  It registers
//   AckedSeqnumCallback, but passes a nullptr privdata.  However this callback
//   needs the privdata to store user's done callback index.
//
// ...But I think these issues can be addressed.

class Table {
 public:
  // Add an entry asynchronously.  This function immediately returns.  On
  // finalization of the write, "done" will be fired.
  Status Add(const JobID &job_id,
             const ID &id,
             std::shared_ptr<DataT> data,
             const Callback &done) {
    // ... Form "key", "val", etc.
    return gcs_.Add(key, val, done_callback_idx);
  }

 private:
  GcsHandle gcs_;
};

// A handle to a logical GCS shard.
//
// Behind the scene, the logical shard this points to may be backed by replicas.
//
// Typical usage:
//
//     RETURN_IF_ERROR(gcs.Connect(...).ok());
//     RETURN_IF_ERROR(gcs.AttachToEventLoop(loop).ok());
//     RETURN_IF_ERROR(gcs.Add(...).ok());
class GcsHandle {
 public:
  // Hostname is a (address, port) pair.
  using Hostname = std::pair<std::string, std::string>;
  Status Connect(const Hostname &master,
                 const Hostname &write,
                 const Hostname &read);
  Status AttachToEventLoop(aeEventLoop *loop) {
    // aeAttach() first...
    // ...then subscribe via the ack_context_.
    const int status = redisAsyncCommand(
        ack_subscribe_context_, &AckedSeqnumCallback,
        /*privdata=*/NULL, "SUBSCRIBE %b", kChan.c_str(), kChan.size());
    // This ordering is necessary to work around a hiredis assumption.
  }

  // Non-blocking.  User's callback will be fired when the write is finalizd.
  Status Add(const std::string &key,
             const std::string &val,
             int done_callback_idx) {
    write_context_->redisAsyncCommand("MEMBER.PUT %s %s",
                                      /*callback=*/AssignedSeqnumCallback,
                                      /*privdata=*/done_callback_idx, key, val);
  }

 private:
  // The actual hiredis contexts.
  redisAsyncContext *master_context_, write_context_, ack_context_,
      read_context_;

  // Client's bookkeeping for seqnums.
  std::unordered_set<int64_t> assigned_seqnums;
  std::unordered_set<int64_t> acked_seqnums;

  // Callbacks: common to all Put().

  // Gets fired whenever an ACK from the store comes back.
  void AckedSeqnumCallback(redisAsyncContext *ack_context,  // != write_context.
                           void *r,                         // reply
                           void *privdata) {
    const redisReply *reply = reinterpret_cast<redisReply *>(r);
    const redisReply *message_type = reply->element[0];
    if (reply->element[2]->str == nullptr) {
      // Subscribed.
      return;
    }
    const int64_t received_sn = std::stoi(reply->element[2]->str);
    auto it = assigned_seqnums.find(received_sn);
    if (it == assigned_seqnums.end()) {
      acked_seqnums.insert(received_sn);
      return;
    }
    // Launch the user "done" callback.
    assigned_seqnums.erase(it);
    RedisCallbackManager::instance().get(privdata)(...);
  }
  // Gets fired whenever the store assigns a seqnum for a Put request.
  void AssignedSeqnumCallback(
      redisAsyncContext *write_context,  // != ack_context.
      void *r,                           // reply
      void *privdata) {
    const redisReply *reply = reinterpret_cast<redisReply *>(r);
    const int64_t assigned_seqnum = reply->integer;
    auto it = acked_seqnums.find(assigned_seqnum);
    if (it != acked_seqnums.end()) {
      acked_seqnums.erase(it);
      // Launch the user "done" callback.
      RedisCallbackManager::instance().get(privdata)(...);
    } else {
      assigned_seqnums.insert(assigned_seqnum);
    }
  }
};
