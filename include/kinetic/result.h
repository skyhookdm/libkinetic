enum status_code {
    // Not an error
    OK                                     ,

    CLIENT_IO_ERROR                        ,
    CLIENT_SHUTDOWN                        ,
    CLIENT_INTERNAL_ERROR                  ,
    CLIENT_RESPONSE_HMAC_VERIFICATION_ERROR,

    REMOTE_HMAC_ERROR                      ,
    REMOTE_NOT_AUTHORIZED                  ,

    REMOTE_VERSION_MISMATCH                ,
    REMOTE_CLUSTER_VERSION_MISMATCH        ,

    REMOTE_INVALID_REQUEST                 ,
    REMOTE_HEADER_REQUIRED                 ,
    REMOTE_INTERNAL_ERROR                  ,
    REMOTE_SERVICE_BUSY                    ,
    REMOTE_NOT_FOUND                       ,

    REMOTE_EXPIRED                         ,
    REMOTE_DATA_ERROR                      ,
    REMOTE_PERM_DATA_ERROR                 ,
    REMOTE_REMOTE_CONNECTION_ERROR         ,

    REMOTE_NO_SPACE                        ,
    REMOTE_NO_SUCH_HMAC_ALGORITHM          ,
    REMOTE_OTHER_ERROR                     ,
    REMOTE_NESTED_OPERATION_ERRORS         ,

    PROTOCOL_ERROR_RESPONSE_NO_ACKSEQUENCE ,
};

struct callback_result {
    enum status_code result_status;
}

// status status_from_rpc ( <what goes here> );
