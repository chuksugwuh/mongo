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

#pragma once

#include <map>

#include "mongo/db/dist_cache.h"
#include "mongo/db/operation_context.h"
#include "mongo/db/repl/read_concern_args.h"
#include "mongo/db/rw_concern_default_gen.h"
#include "mongo/db/service_context.h"
#include "mongo/db/write_concern_options.h"
#include "mongo/platform/mutex.h"
#include "mongo/util/concurrency/with_lock.h"

namespace mongo {

/**
 * Class to manage Read Concern and Write Concern (RWC) defaults.
 */
class ReadWriteConcernDefaults {
public:
    /**
     * Consistent names for the classes that define a ReadConcern and a WriteConcern.
     */
    using ReadConcern = repl::ReadConcernArgs;
    using WriteConcern = WriteConcernOptions;

    using LookupFn = std::function<boost::optional<RWConcernDefault>(OperationContext*)>;

    static constexpr StringData readConcernFieldName = ReadConcern::kReadConcernFieldName;
    static constexpr StringData writeConcernFieldName = WriteConcern::kWriteConcernField;

    static ReadWriteConcernDefaults& get(ServiceContext* service);
    static ReadWriteConcernDefaults& get(ServiceContext& service);
    static void create(ServiceContext* service, LookupFn lookupFn);

    ReadWriteConcernDefaults() = delete;
    ReadWriteConcernDefaults(LookupFn lookupFn);
    ~ReadWriteConcernDefaults() = default;

    /**
     * Returns true if the RC level is permissible to use as a default, and false if it cannot be a
     * RC default.
     */
    static bool isSuitableReadConcernLevel(repl::ReadConcernLevel level);

    /**
     * Checks if the given RWC is suitable to use as a default, and uasserts if not.
     */
    static void checkSuitabilityAsDefault(const ReadConcern& rc);
    static void checkSuitabilityAsDefault(const WriteConcern& wc);

    /**
     * Interface when an admin has run the command to change the defaults.
     * At least one of the `rc` or `wc` params must be set.
     * Will generate and use a new epoch and setTime for the updated defaults, which are returned.
     */
    RWConcernDefault setConcerns(OperationContext* opCtx,
                                 const boost::optional<ReadConcern>& rc,
                                 const boost::optional<WriteConcern>& wc);

    /**
     * Invalidates the cached RWC defaults, causing them to be refreshed.
     *
     * After this call returns, the read methods below (getDefault, getDefaultReadConcern,
     * getDefaultWriteConcern) may continue returning the invalidated defaults, until they have been
     * replaced by the refreshed values.  This is to avoid stalling CRUD ops (and other ops that
     * need RC/WC) during refresh.
     */
    void invalidate();

    RWConcernDefault getDefault(OperationContext* opCtx);
    boost::optional<ReadConcern> getDefaultReadConcern(OperationContext* opCtx);
    boost::optional<WriteConcern> getDefaultWriteConcern(OperationContext* opCtx);

private:
    enum class Type { kReadWriteConcernEntry };

    void _setDefault(RWConcernDefault&& rwc);
    boost::optional<RWConcernDefault> _getDefault(OperationContext* opCtx);

    class Cache : public DistCache<Type, RWConcernDefault> {
        Cache(const Cache&) = delete;
        Cache& operator=(const Cache&) = delete;

    public:
        Cache(LookupFn lookupFn);
        virtual ~Cache() = default;

    private:
        boost::optional<RWConcernDefault> lookup(OperationContext* opCtx, const Type& key) override;

        // For exclusive use by DistCache only.
        Mutex _mutex = MONGO_MAKE_LATCH("ReadWriteConcernDefaults::Cache");

        LookupFn _lookupFn;
    };

    Cache _defaults;
};

}  // namespace mongo
