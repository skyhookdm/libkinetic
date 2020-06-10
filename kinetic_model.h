/**
 * Copyright 2013-2015 Seagate Technology LLC.
 *
 * This Source Code Form is subject to the terms of the Mozilla
 * Public License, v. 2.0. If a copy of the MPL was not
 * distributed with this file, You can obtain one at
 * https://mozilla.org/MP:/2.0/.
 *
 * This program is distributed in the hope that it will be useful,
 * but is provided AS-IS, WITHOUT ANY WARRANTY; including without
 * the implied warranty of MERCHANTABILITY, NON-INFRINGEMENT or
 * FITNESS FOR A PARTICULAR PURPOSE. See the Mozilla Public
 * License for more details.
 *
 * See www.openkinetic.org for more project information
 */

#ifndef KINETIC_CONNECTION_MODEL_H_
#define KINETIC_CONNECTION_MODEL_H_

#include "kinetic_client.pb.h"

#include "kinetic/common.h"
#include "kinetic/drive_log.h"
#include "kinetic/acls.h"

#include "kinetic/kinetic_record.h"
#include "kinetic/kinetic_connection.h"
#include "kinetic/kinetic_status.h"
#include "kinetic/nonblocking_packet_service_interface.h"

#include <memory>
#include <string>
#include <vector>
#include <list>

namespace kinetic {

    // Generated protobuf code
    using com::seagate::kinetic::client::proto::Command;
    using com::seagate::kinetic::client::proto::Command_MessageType;
    using com::seagate::kinetic::client::proto::Command_P2POperation;
    using com::seagate::kinetic::client::proto::Command_Synchronization;
    using com::seagate::kinetic::client::proto::Command_GetLog_Type;
    using com::seagate::kinetic::client::proto::Command_Priority;
    using com::seagate::kinetic::client::proto::Command_Range;

    using std::shared_ptr;
    using std::unique_ptr;
    using std::string;
    using std::make_shared;
    using std::list;
    using std::vector;


    // A forward declaration so that it can be referenced in P2PPushOperation
    struct P2PPushRequest;

    /// Represents a single P2P copy operation
    struct P2PPushOperation {
        string key;
        string version;

        /// (Optional) specify a different name for the key on the remote drive.
        string remoteKey;

        bool force;

        /// A request to be invoked after this operation.
        //  NOTE: not sure when this resolves, or if it matters (only reflects happens before)
        P2PPushRequest *request;
    };

    /// Represents a collection of P2P operations
    struct P2PPushRequest {
        string host;
        int port;

        vector<P2PPushOperation> operations;
    };

    class NonblockingKineticConnectionInterface {
    };

} // namespace kinetic

#endif  // KINETIC_CONNECTION_MODEL_H_
