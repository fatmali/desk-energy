#include <assert.h>
#include <stdio.h>

#include "DeskState.h"

int main() {
  assert(validDisplayText("", 48));
  assert(validDisplayText("Finish this task", 48));
  assert(!validDisplayText("bad\ntext", 48));
  assert(!validDisplayText(std::string("a\0b", 3), 48));
  assert(!validDisplayText(std::string(49, 'x'), 48));

  const std::string first(32, 'a'), second(32, 'b');
  JobStatus job;
  assert(job.state == "none");
  assert(!job.report(first, "Tests", "test", "passed", 0));
  assert(!job.report("bad", "Tests", "test", "running", 0));
  assert(!job.report(first, "", "test", "running", 0));
  assert(!job.report(first, "Tests", "invalid", "running", 0));
  assert(job.report(first, "Tests", "test", "running", UINT32_MAX - 1000));
  assert(!job.expire(1000));
  assert(job.expire(59000));
  assert(job.state == "unknown");
  assert(job.report(first, "Tests", "test", "heartbeat", 60000));
  assert(job.state == "running");
  assert(job.report(second, "Firmware", "build", "running", 60001));
  assert(!job.report(first, "Tests", "test", "heartbeat", 60002));
  assert(!job.report(first, "Tests", "test", "passed", 60003));
  assert(job.report(second, "Firmware", "build", "failed", 60004));
  assert(job.state == "failed");
  assert(!job.report(second, "Firmware", "build", "heartbeat", 60005));
  assert(!job.expire(200000));
  assert(job.report(first, "Tests", "test", "running", 200001));
  assert(job.report(first, "Tests", "test", "cancelled", 200002));
  puts("PASS: text validation, job ordering, heartbeat expiry and rollover");
}
