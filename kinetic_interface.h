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

#ifndef KINETIC_CONNECTION_H_
#define KINETIC_CONNECTION_H_

#include "nonblocking_kinetic_connection_interface.h"

namespace kinetic {

    class NonblockingKineticConnection : public NonblockingKineticConnectionInterface {
        public:
            explicit NonblockingKineticConnection(NonblockingPacketServiceInterface *service);

            ~NonblockingKineticConnection();

            bool Run(fd_set *read_fds, fd_set *write_fds, int *nfds);
            bool RemoveHandler(HandlerKey handler_key);
            void SetClientClusterVersion(int64_t cluster_version);

            HandlerKey NoOp(const shared_ptr<SimpleCallbackInterface> callback);
            HandlerKey Flush(const shared_ptr<SimpleCallbackInterface> callback);

            // ------------------------------
            /** Misc operations **/
            HandlerKey GetLog(
                const vector<Command_GetLog_Type> &types,
                const shared_ptr<GetLogCallbackInterface> callback
            );

            HandlerKey UpdateFirmware(
                const shared_ptr<const string> new_firmware,
                const shared_ptr<SimpleCallbackInterface> callback
            );

            HandlerKey SetClusterVersion(int64_t new_cluster_version, const shared_ptr<SimpleCallbackInterface> callback);

            // ------------------------------
            /** Key Operations **/
            HandlerKey Put(
                const string key, const string current_version, WriteMode mode,
                const shared_ptr<const KineticRecord> record,
                const shared_ptr<PutCallbackInterface> callback,
                PersistMode persistMode
            );

            HandlerKey Delete(
                const string key, const string version, WriteMode mode,
                const shared_ptr<SimpleCallbackInterface> callback,
                PersistMode persistMode
            );

            HandlerKey Get(const string key, const shared_ptr<GetCallbackInterface> callback);
            HandlerKey GetVersion(const string key, const shared_ptr<GetVersionCallbackInterface> callback);

            // ------------------------------
            /** B-tree Operations **/
            HandlerKey GetNext(const string key, const shared_ptr<GetCallbackInterface> callback);
            HandlerKey GetPrevious(const string key, const shared_ptr<GetCallbackInterface> callback);

            HandlerKey GetKeyRange(
                const string start_key, bool start_key_inclusive,
                const string end_key  , bool end_key_inclusive,
                bool reverse_results  , int32_t max_results,
                const shared_ptr<GetKeyRangeCallbackInterface> callback
            );

            // ------------------------------
            /** Media Operations **/
            HandlerKey MediaScan(
                const string start_key, bool start_key_inclusive,
                const string end_key  , bool end_key_inclusive,
                int32_t max_results,
                const shared_ptr<MediaScanCallbackInterface> callback
            );

            HandlerKey MediaOptimize(
                const string start_key, bool start_key_inclusive,
                const string end_key  , bool end_key_inclusive,
                const shared_ptr<MediaOptimizeCallbackInterface> callback
            );

            // ------------------------------
            /** Peer to Peer Operations **/
            HandlerKey P2PPush(
                const P2PPushRequest &push_request,
                const shared_ptr<P2PPushCallbackInterface> callback
            );

            // ------------------------------
            /** Security (and secured) Operations **/
            HandlerKey LockDevice(const string pin, const shared_ptr<SimpleCallbackInterface> callback);
            HandlerKey UnlockDevice(const string pin, const shared_ptr<SimpleCallbackInterface> callback);

            HandlerKey SetErasePIN(
                const string new_pin, const string current_pin,
                const shared_ptr<SimpleCallbackInterface> callback
            );

            HandlerKey SetLockPIN(
                const string new_pin, const string current_pin,
                const shared_ptr<SimpleCallbackInterface> callback
            );

            HandlerKey InstantErase(const string pin, const shared_ptr<SimpleCallbackInterface> callback);
            HandlerKey SecureErase(const string pin, const shared_ptr<SimpleCallbackInterface> callback);

            HandlerKey SetACLs(
                const shared_ptr<const list<ACL>> acls,
                const shared_ptr<SimpleCallbackInterface> callback
            );

        private HandlerKey GenericGet(
            const shared_ptr<const string> key,
            const shared_ptr<GetCallbackInterface> callback,
            Command_MessageType message_type
        );

        private void PopulateP2PMessage(
            Command_P2POperation *mutable_p2pop,
            const shared_ptr<const P2PPushRequest> push_request
        );

        private unique_ptr<Command> NewCommand(Command_MessageType message_type);
        private Command_Synchronization GetSynchronizationForPersistMode(PersistMode persistMode);

        private NonblockingPacketServiceInterface *service_;
        private const shared_ptr<const string> empty_str_;

        private int64_t cluster_version_;
    };

} // namespace kinetic

#endif  // KINETIC_CONNECTION_H_
