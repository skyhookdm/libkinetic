#include <stdint.h>

#include "kinetic/result.h"

// ------------------------------
// Functions for Kinetic API operations

/** Simple function that only returns success or failure **/
void callback_success_simple();
void callback_failure_simple(struct callback_result *kinetic_result);

void handle_simple(const struct command *response, const char *value);
void error_simple(const struct command *response, struct callback_result *kinetic_result);


/** Get **/
void callback_success_get(const char *key, struct data_tuple *record);
void callback_failure_get(struct callback_result *kinetic_result);

void handle_get(const struct command *response, const char *value);
void error_get(const struct command *response, struct callback_result *kinetic_result);


/** Get Version **/
void callback_success_getversion(const char *key, struct data_tuple *record);
void callback_failure_getversion(struct callback_result *kinetic_result);

void handle_getversion(const struct command *response, const char *value);
void error_getversion(const struct command *response, struct callback_result *kinetic_result);


/** Get Key Range **/
void callback_success_getkeyrange(const char **keys);
void callback_failure_getkeyrange(struct callback_result *kinetic_result);

void handle_getkeyrange(const struct command *response, const char *value);
void error_getkeyrange(const struct command *response, struct callback_result *kinetic_result);


/** Scan Media **/
void callback_success_scanmedia(const char **keys, const char *last_key);
void callback_failure_scanmedia(struct callback_result *kinetic_result);

void handle_scanmedia(const struct command *response, const char *value);
void error_scanmedia(const struct command *response, struct callback_result *kinetic_result);

// TODO

class MediaOptimizeCallbackInterface {
  public:
    virtual ~MediaOptimizeCallbackInterface() {}

    virtual void Success(const std::string &last_key) = 0;

    virtual void Failure(KineticStatus error) = 0;
};

class MediaOptimizeHandler : public HandlerInterface {
    public explicit MediaOptimizeHandler(const shared_ptr<MediaOptimizeCallbackInterface> callback);

    public void Handle(const Command &response, unique_ptr<const string> value);
    public void Error(KineticStatus error, Command const *const response);

    private const shared_ptr<MediaOptimizeCallbackInterface> callback_;
};

class PutCallbackInterface {
    public virtual ~PutCallbackInterface() {}

    public virtual void Success() = 0;
    public virtual void Failure(KineticStatus error) = 0;
};

class PutHandler : public HandlerInterface {
    public explicit PutHandler(const shared_ptr<PutCallbackInterface> callback);

    public void Handle(const Command &response, unique_ptr<const string> value);
    public void Error(KineticStatus error, Command const *const response);

    private const shared_ptr<PutCallbackInterface> callback_;
};

class GetLogCallbackInterface {
    public virtual ~GetLogCallbackInterface() {}

    public virtual void Success(unique_ptr<DriveLog> drive_log) = 0;
    public virtual void Failure(KineticStatus error) = 0;
};

class GetLogHandler : public HandlerInterface {
    public explicit GetLogHandler(const shared_ptr<GetLogCallbackInterface> callback);

    public void Handle(const Command &response, unique_ptr<const string> value);
    public void Error(KineticStatus error, Command const *const response);

    private const shared_ptr<GetLogCallbackInterface> callback_;
};

class P2PPushCallbackInterface {
    public virtual void Success(unique_ptr<vector<KineticStatus>> operation_statuses, const Command &response) = 0;
    public virtual void Failure(KineticStatus error, Command const *const response) = 0;
};

class P2PPushHandler : public HandlerInterface {
    public explicit P2PPushHandler(const shared_ptr<P2PPushCallbackInterface> callback);

    public void Handle(const Command &response, unique_ptr<const string> value);
    public void Error(KineticStatus error, Command const *const response);

    private const shared_ptr<P2PPushCallbackInterface> callback_;
};
