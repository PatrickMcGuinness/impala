// Copyright 2012 Cloudera Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "runtime/exec-env.h"

#include <vector>

#include <boost/algorithm/string.hpp>

#include "common/logging.h"
#include "common/service-ids.h"
#include "runtime/client-cache.h"
#include "runtime/data-stream-mgr.h"
#include "runtime/disk-io-mgr.h"
#include "runtime/hbase-table-cache.h"
#include "runtime/hdfs-fs-cache.h"
#include "statestore/simple-scheduler.h"
#include "statestore/subscription-manager.h"
#include "util/metrics.h"
#include "util/webserver.h"
#include "util/default-path-handlers.h"
#include "gen-cpp/ImpalaInternalService.h"

using namespace std;
using namespace boost;

DEFINE_bool(use_statestore, true,
    "Use an external state-store process to manage cluster membership");
DEFINE_bool(enable_webserver, true, "If true, debug webserver is enabled");
DECLARE_int32(be_port);
DECLARE_string(ipaddress);

namespace impala {

ExecEnv::ExecEnv()
  : stream_mgr_(new DataStreamMgr()),
    subscription_mgr_(new SubscriptionManager()),
    client_cache_(new BackendClientCache(0, 0)),
    fs_cache_(new HdfsFsCache()),
    htable_cache_(new HBaseTableCache()),
    disk_io_mgr_(new DiskIoMgr()),
    webserver_(new Webserver()),
    metrics_(new Metrics()),
    enable_webserver_(FLAGS_enable_webserver),
    tz_database_(TimezoneDatabase()) {
  // Initialize the scheduler either dynamically (with a statestore) or statically (with
  // a standalone single backend)
  if (FLAGS_use_statestore) {
    scheduler_.reset(new SimpleScheduler(subscription_mgr_.get(), IMPALA_SERVICE_ID,
        metrics_.get()));
  } else {
    vector<THostPort> addresses;
    THostPort address;
    address.ipaddress = FLAGS_ipaddress;
    address.port = FLAGS_be_port;
    addresses.push_back(address);
    scheduler_.reset(new SimpleScheduler(addresses, metrics_.get()));
  }
  Status status = disk_io_mgr_->Init();
  CHECK(status.ok());
}

ExecEnv::~ExecEnv() {
  if (scheduler_ != NULL) scheduler_->Close();
}

Status ExecEnv::StartServices() {
  LOG(INFO) << "Starting global services";
  // Start services in order to ensure that dependencies between them are met
  if (enable_webserver_) {
    AddDefaultPathHandlers(webserver_.get());
    RETURN_IF_ERROR(webserver_->Start());
  } else {
    LOG(INFO) << "Not starting webserver";
  }

  metrics_->Init(enable_webserver_ ? webserver_.get() : NULL);

  if (FLAGS_use_statestore) RETURN_IF_ERROR(subscription_mgr_->Start());

  if (scheduler_ != NULL) RETURN_IF_ERROR(scheduler_->Init());

  return Status::OK;
}

}
