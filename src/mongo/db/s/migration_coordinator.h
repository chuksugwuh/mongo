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

#include "mongo/db/s/migration_coordinator_document_gen.h"
#include "mongo/s/catalog/type_chunk.h"

namespace mongo {

namespace migrationutil {
/**
 * Manages the migration commit/abort process, including updates to config.rangeDeletions on the
 * donor and the recipient, and updates to the routing table on the config server.
 *
 * TODO (SERVER-44716): Implement commit/abort/recovery logic on the config server.
 */
class MigrationCoordinator {
public:
    MigrationCoordinator(OperationContext* opCtx,
                         UUID migrationId,
                         ShardId donorShard,
                         ShardId recipientShard,
                         NamespaceString collectionNamespace,
                         UUID collectionUuid,
                         ChunkRange range);
    MigrationCoordinator(const MigrationCoordinator&) = delete;
    MigrationCoordinator& operator=(const MigrationCoordinator&) = delete;
    MigrationCoordinator(MigrationCoordinator&&) = delete;
    MigrationCoordinator& operator=(MigrationCoordinator&&) = delete;

    ~MigrationCoordinator();

    /**
     * Initializes persistent state required to ensure that orphaned ranges are properly handled,
     * even after failover, by doing the following:
     *
     * 1) Inserts a document into the local config.migrationCoordinators with the lsid, txnNumber,
     * and recipientId and waits for majority writeConcern.
     * 2) Inserts a document into the local config.rangeDeletions with the collectionUUID, range to
     * delete, and "pending: true" and waits for majority writeConcern.
     */
    void startMigration(OperationContext* opCtx, bool waitForDelete);

    /**
     * Deletes the range deletion task from the recipient node and marks the range deletion task on
     * the donor as ready to be processed.
     */
    void commitMigrationOnDonorAndRecipient(OperationContext* opCtx);

    /**
     * Deletes the range deletion task from the donor node and marks the range deletion task on the
     * recipient node as ready to be processed.
     */
    void abortMigrationOnDonorAndRecipient(OperationContext* opCtx);

private:
    MigrationCoordinatorDocument _migrationInfo;
};

}  // namespace migrationutil
}  // namespace mongo
