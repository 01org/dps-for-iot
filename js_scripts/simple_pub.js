"use strict";
var dps = require("dps");

(function () {
    var keyID = [
        [0xed, 0x54, 0x14, 0xa8, 0x5c, 0x4d, 0x4d, 0x15, 0xb6, 0x9f, 0x0e, 0x99, 0x8a, 0xb1, 0x71, 0xf2],
        [0x53, 0x4d, 0x2a, 0x4b, 0x98, 0x76, 0x1f, 0x25, 0x6b, 0x78, 0x3c, 0xc2, 0xf8, 0x12, 0x90, 0xcc]
    ];
    var networkKeyID = [
        0x4c, 0xfc, 0x6b, 0x75, 0x0f, 0x80, 0x95, 0xb3, 0x6c, 0xb7, 0xc1, 0x2f, 0x65, 0x2d, 0x38, 0x26
    ];
    /* Pre-shared keys for testing only. DO NOT USE THESE KEYS IN A REAL APPLICATION! */
    var keyData = [
        [0x77, 0x58, 0x22, 0xfc, 0x3d, 0xef, 0x48, 0x88, 0x91, 0x25, 0x78, 0xd0, 0xe2, 0x74, 0x5c, 0x10],
        [0x39, 0x12, 0x3e, 0x7f, 0x21, 0xbc, 0xa3, 0x26, 0x4e, 0x6f, 0x3a, 0x21, 0xa4, 0xf1, 0xb5, 0x98]
    ];
    var networkKey = [
        0xcd, 0xfe, 0x31, 0x59, 0x70, 0x5f, 0xe4, 0xc8, 0xcb, 0x40, 0xac, 0x69, 0x9c, 0x06, 0x3a, 0x1d
    ];
    var keyStore;
    var node;
    var pub;
    var i;
    var onAck = function (pub, payload) {
        console.log("Ack for pub UUID " + dps.publicationGetUUID(pub) + "(" + dps.publicationGetSequenceNum(pub) + ")");
        console.log("    " + payload);
    };
    var stop = function () {
        dps.destroyPublication(pub);
        dps.destroyNode(node);
        dps.destroyMemoryKeyStore(keyStore);
    };
    var publish = function () {
        dps.publish(pub, "world", 0);
        setTimeout(stop, 100);
    };
    /* Set to 1 to enable DPS debug output */
    dps.debug = 1;

    keyStore = dps.createMemoryKeyStore();
    for (i = 0; i < keyID.length; i += 1) {
        dps.setContentKey(keyStore, keyID[i], keyData[i]);
    }
    dps.setNetworkKey(keyStore, networkKeyID, networkKey);

    node = dps.createNode("/", dps.memoryKeyStoreHandle(keyStore), keyID[0]);
    dps.startNode(node, dps.MCAST_PUB_ENABLE_SEND, 0);
    pub = dps.createPublication(node);

    dps.initPublication(pub, ["a/b/c"], false, null, onAck);
    dps.publish(pub, "hello", 0);
    setTimeout(publish, 100);
}());
