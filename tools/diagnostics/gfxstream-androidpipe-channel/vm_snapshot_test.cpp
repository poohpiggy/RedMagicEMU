#include "vm_snapshot.h"
#include "host-common/vm_operations.h"
#include <cassert>
#include <cstdio>

int main() {
    assert(!rme::snapshotVmAgent());
    assert(!rme::bindSnapshotVm(1, rme::SnapshotCapability::Unknown));
    assert(!rme::bindSnapshotVm(1, rme::SnapshotCapability::Supported));
    assert(!rme::snapshotVmAgent());
    uint64_t token = rme::bindSnapshotVm(1, rme::SnapshotCapability::Unavailable);
    assert(token);
    const auto* vm = rme::snapshotVmAgent();
    assert(vm && vm->setSkipSnapshotSave && vm->isSnapshotSaveSkipped);
    assert(!vm->snapshotSave && !vm->snapshotLoad);
    bool requested = true;
    assert(rme::readSnapshotSkipRequest(token, 1, &requested) && !requested);
    vm->setSkipSnapshotSave(true);
    assert(rme::readSnapshotSkipRequest(token, 1, &requested) && requested);
    assert(vm->isSnapshotSaveSkipped());
    vm->setSkipSnapshotSave(false);
    assert(rme::readSnapshotSkipRequest(token, 1, &requested) && !requested);
    assert(vm->isSnapshotSaveSkipped()); // No VM snapshot support even if requested.
    assert(!rme::clearSnapshotVm(token, 2));
    assert(rme::clearSnapshotVm(token, 1));
    assert(!rme::snapshotVmAgent());
    uint64_t next = rme::bindSnapshotVm(1, rme::SnapshotCapability::Unavailable);
    assert(next && next != token && !rme::clearSnapshotVm(token, 1));
    assert(rme::clearSnapshotVm(next, 1));
    std::puts("SNAPSHOT_UNAVAILABLE_VM_CALLBACK_OK");
}
