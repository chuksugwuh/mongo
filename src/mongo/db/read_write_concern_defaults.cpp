/**
 *    Copyright (C) 2019-present MongoDB, Inc.
 *
 *    This program is free software: you can redistribute it and/or modify
 *    it under the terms of the Server Side Public License, version 1,
 *    as published by MongoDB, Inc.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    Server Side Public License for more details.
 *
 *    You should have received a copy of the Server Side Public License
 *    along with this program. If not, see
 *    <http://www.mongodb.com/licensing/server-side-public-license>.
 *
 *    As a special exception, the copyright holders give permission to link the
 *    code of portions of this program with the OpenSSL library under certain
 *    conditions as described in each individual source file and distribute
 *    linked combinations including the program with the OpenSSL library. You
 *    must comply with the Server Side Public License in all respects for
 *    all of the code used other than as permitted herein. If you modify file(s)
 *    with this exception, you may extend this exception to your version of the
 *    file(s), but you are not obligated to do so. If you do not wish to do so,
 *    delete this exception statement from your version. If you delete this
 *    exception statement from all source files in the program, then also delete
 *    it in the license file.
 */

#include "mongo/platform/basic.h"

#include "mongo/db/read_write_concern_defaults.h"

#include "mongo/db/logical_clock.h"

namespace mongo {

namespace {

static constexpr auto kReadConcernLevelsDisallowedAsDefault = {
    repl::ReadConcernLevel::kSnapshotReadConcern, repl::ReadConcernLevel::kLinearizableReadConcern};

}

bool ReadWriteConcernDefaults::isSuitableReadConcernLevel(repl::ReadConcernLevel level) {
    for (auto bannedLevel : kReadConcernLevelsDisallowedAsDefault) {
        if (level == bannedLevel) {
            return false;
        }
    }
    return true;
}

void ReadWriteConcernDefaults::checkSuitabilityAsDefault(const ReadConcern& rc) {
    uassert(ErrorCodes::BadValue,
            str::stream() << "level: '" << repl::readConcernLevels::toString(rc.getLevel())
                          << "' is not suitable for the default read concern",
            isSuitableReadConcernLevel(rc.getLevel()));
    uassert(ErrorCodes::BadValue,
            str::stream() << "'" << ReadConcern::kAfterOpTimeFieldName
                          << "' is not suitable for the default read concern",
            !rc.getArgsOpTime());
    uassert(ErrorCodes::BadValue,
            str::stream() << "'" << ReadConcern::kAfterClusterTimeFieldName
                          << "' is not suitable for the default read concern",
            !rc.getArgsAfterClusterTime());
    uassert(ErrorCodes::BadValue,
            str::stream() << "'" << ReadConcern::kAtClusterTimeFieldName
                          << "' is not suitable for the default read concern",
            !rc.getArgsAtClusterTime());
}

void ReadWriteConcernDefaults::checkSuitabilityAsDefault(const WriteConcern& wc) {
    uassert(ErrorCodes::BadValue,
            "Unacknowledged write concern is not suitable for the default write concern",
            !(wc.wMode.empty() && wc.wNumNodes < 1));
}

void ReadWriteConcernDefaults::_setDefault(RWConcernDefault&& rwc) {
    _defaults.revalidate(Type::kReadWriteConcernEntry, std::move(rwc));
}

RWConcernDefault ReadWriteConcernDefaults::setConcerns(OperationContext* opCtx,
                                                       const boost::optional<ReadConcern>& rc,
                                                       const boost::optional<WriteConcern>& wc) {
    invariant(rc || wc);

    if (rc) {
        checkSuitabilityAsDefault(*rc);
    }
    if (wc) {
        checkSuitabilityAsDefault(*wc);
    }

    auto now = opCtx->getServiceContext()->getFastClockSource()->now();
    auto epoch = LogicalClock::get(opCtx->getServiceContext())->getClusterTime().asTimestamp();

    RWConcernDefault rwc;
    rwc.setDefaultReadConcern(rc);
    rwc.setDefaultWriteConcern(wc);
    rwc.setEpoch(epoch);
    rwc.setSetTime(now);
    rwc.setLocalSetTime(now);

    auto current = _getDefault(opCtx);
    if (!rc && current) {
        rwc.setDefaultReadConcern(current->getDefaultReadConcern());
    }
    if (!wc && current) {
        rwc.setDefaultWriteConcern(current->getDefaultWriteConcern());
    }
    _setDefault(std::move(rwc));
    return *_getDefault(opCtx);
}

void ReadWriteConcernDefaults::invalidate() {
    _defaults.invalidate(Type::kReadWriteConcernEntry);
}

boost::optional<RWConcernDefault> ReadWriteConcernDefaults::_getDefault(OperationContext* opCtx) {
    auto defaultsHandle = _defaults.acquire(opCtx, Type::kReadWriteConcernEntry);
    if (defaultsHandle) {
        auto& defaultsValue = **defaultsHandle;
        // Since CWRWC is ok with continuing to use a value well after it has been invalidated
        // (since RWC defaults apply for the lifetime of the op/cursor), we don't need to check
        // defaultsValue.isValid() here, and we don't need to return the Handle, since callers don't
        // need to check defaultsValue.isValid() later, either.  Just dereference it to get the
        // underlying contents.
        return *defaultsValue;
    }
    return boost::none;
}

RWConcernDefault ReadWriteConcernDefaults::getDefault(OperationContext* opCtx) {
    return _getDefault(opCtx).value_or(RWConcernDefault());
}

boost::optional<ReadWriteConcernDefaults::ReadConcern>
ReadWriteConcernDefaults::getDefaultReadConcern(OperationContext* opCtx) {
    auto current = getDefault(opCtx);
    return current.getDefaultReadConcern();
}

boost::optional<ReadWriteConcernDefaults::WriteConcern>
ReadWriteConcernDefaults::getDefaultWriteConcern(OperationContext* opCtx) {
    auto current = getDefault(opCtx);
    return current.getDefaultWriteConcern();
}

namespace {

const auto getReadWriteConcernDefaults =
    ServiceContext::declareDecoration<std::unique_ptr<ReadWriteConcernDefaults>>();

}  // namespace

ReadWriteConcernDefaults& ReadWriteConcernDefaults::get(ServiceContext* service) {
    return *getReadWriteConcernDefaults(service);
}

ReadWriteConcernDefaults& ReadWriteConcernDefaults::get(ServiceContext& service) {
    return *getReadWriteConcernDefaults(service);
}

void ReadWriteConcernDefaults::create(ServiceContext* service, LookupFn lookupFn) {
    getReadWriteConcernDefaults(service) = std::make_unique<ReadWriteConcernDefaults>(lookupFn);
}

ReadWriteConcernDefaults::ReadWriteConcernDefaults(LookupFn lookupFn) : _defaults(lookupFn) {}

ReadWriteConcernDefaults::Cache::Cache(LookupFn lookupFn)
    : DistCache(1, _mutex), _lookupFn(lookupFn) {}

boost::optional<RWConcernDefault> ReadWriteConcernDefaults::Cache::lookup(
    OperationContext* opCtx, const ReadWriteConcernDefaults::Type& key) {
    invariant(key == Type::kReadWriteConcernEntry);
    // TODO: failpoint to uassert a custom Status
    return _lookupFn(opCtx);
}

}  // namespace mongo
