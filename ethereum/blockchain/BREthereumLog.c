//
//  BREthereumLog.c
//  BRCore
//
//  Created by Ed Gamble on 5/10/18.
//  Copyright (c) 2018 breadwallet LLC
//
//  Permission is hereby granted, free of charge, to any person obtaining a copy
//  of this software and associated documentation files (the "Software"), to deal
//  in the Software without restriction, including without limitation the rights
//  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
//  copies of the Software, and to permit persons to whom the Software is
//  furnished to do so, subject to the following conditions:
//
//  The above copyright notice and this permission notice shall be included in
//  all copies or substantial portions of the Software.
//
//  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
//  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
//  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
//  THE SOFTWARE.

#include "BRArray.h"
#include "BREthereumLog.h"

/**
 * A Log *cannot* be identified by its associated transaction (hash) - because one transaction
 * can result in multiple Logs (even identical Logs: address, topics, etc).
 *
 * Imagine a Block that includes a Log of interest is announced and chained.  The Log is 'included'.
 * Later another Block arrives with the same Log - the original Log is now 'pending' and the new
 * Log is 'included'.   How do we know that the two Logs are identical?  If we can't tell, then
 * two will be reported to the User - one as included, one as pending - when instead the pending
 * one, being identical, should just be reported as included.
 *
 * We have the same issue with transactions.  When a transaction is pending and a new block is
 * announced we search the pending transactions for a matching hash - if found we update the
 * transation to included.
 *
 * Referring to the Ethereum Yellow Paper, it appears that the only way to disambiguate Logs is
 * using the pair {Transaction-Hash, Receipt-Index}.  [One asumption here is that a given
 * transaction's contract execution must produced Logs is an deterministic order.]
 *
 * General Note: We only see Logs when they are included in a Block.  For every Log we thus know:
 * Block (hash, number, ...), TransactionHash, TransactionIndex, ReceiptIndex.  The 'same' Log
 * my have a different Block and TransactionIndex.
 */
static BREthereumLogTopic empty;

//
// Log Topic
//
static BREthereumLogTopic
logTopicCreateAddress (BREthereumAddress raw) {
    BREthereumLogTopic topic = empty;
    unsigned int addressBytes = sizeof (raw.bytes);
    unsigned int topicBytes = sizeof (topic.bytes);
    assert (topicBytes >= addressBytes);

    memcpy (&topic.bytes[topicBytes - addressBytes], raw.bytes, addressBytes);
    return topic;
}

extern BREthereumBloomFilter
logTopicGetBloomFilter (BREthereumLogTopic topic) {
    BRRlpData data;
    data.bytes = topic.bytes;
    data.bytesCount = sizeof (topic.bytes);
    return bloomFilterCreateData(data);
}

extern BREthereumBloomFilter
logTopicGetBloomFilterAddress (BREthereumAddress address) {
    return logTopicGetBloomFilter (logTopicCreateAddress(address));
}

static int
logTopicMatchesAddressBool (BREthereumLogTopic topic,
                        BREthereumAddress address) {
    return (0 == memcmp (&topic.bytes[0], &empty.bytes[0], 12) &&
            0 == memcmp (&topic.bytes[12], &address.bytes[0], 20));
}

extern BREthereumBoolean
logTopicMatchesAddress (BREthereumLogTopic topic,
                        BREthereumAddress address) {
    return AS_ETHEREUM_BOOLEAN(logTopicMatchesAddressBool(topic, address));
}

extern BREthereumLogTopicString
logTopicAsString (BREthereumLogTopic topic) {
    BREthereumLogTopicString string;
    string.chars[0] = '0';
    string.chars[1] = 'x';
    encodeHex(&string.chars[2], 65, topic.bytes, 32);
    return string;
}

extern BREthereumAddress
logTopicAsAddress (BREthereumLogTopic topic) {
    BREthereumAddress address;
    memcpy (address.bytes, &topic.bytes[12], 20);
    return address;
}

//
// Support
//
static BREthereumLogTopic
logTopicRlpDecodeItem (BRRlpItem item,
                       BRRlpCoder coder) {
    BREthereumLogTopic topic;

    BRRlpData data = rlpDecodeItemBytes(coder, item);
    assert (32 == data.bytesCount);

    memcpy (topic.bytes, data.bytes, 32);
    rlpDataRelease(data);

    return topic;
}

static BRRlpItem
logTopicRlpEncodeItem(BREthereumLogTopic topic,
                      BRRlpCoder coder) {
    return rlpEncodeItemBytes(coder, topic.bytes, 32);
}

static BREthereumLogTopic emptyTopic;

//
// Ethereum Log
//
// A log entry, O, is:
struct BREthereumLogRecord {
    // THIS MUST BE FIRST to support BRSet operations.
    /**
     * The hash - computed from the pair {Transaction-Hash, Receipt-Index} using
     * BREthereumLogStatus
     */
    BREthereumHash hash;

    // a tuple of the logger’s address, Oa;
    BREthereumAddress address;

    // a series of 32-byte log topics, Ot;
    BREthereumLogTopic *topics;

    // and some number of bytes of data, Od
    uint8_t *data;
    uint8_t dataCount;

    // status
    BREthereumLogStatus status;
};

extern void
logInitializeStatus (BREthereumLog log,
                 BREthereumHash transactionHash,
                 size_t transactionReceiptIndex) {
    log->status = logStatusCreate(LOG_STATUS_PENDING, transactionHash, transactionReceiptIndex);
    log->hash = logStatusCreateHash(&log->status);
}

extern BREthereumLogStatus
logGetStatus (BREthereumLog log) {
    return log->status;
}

extern BREthereumHash
logGetHash (BREthereumLog log) {
    return log->hash;
}

extern BREthereumAddress
logGetAddress (BREthereumLog log) {
    return log->address;
}

extern BREthereumBoolean
logHasAddress (BREthereumLog log,
               BREthereumAddress address) {
    return addressEqual(log->address, address);
}

extern size_t
logGetTopicsCount (BREthereumLog log) {
    return array_count(log->topics);
}

extern  BREthereumLogTopic
logGetTopic (BREthereumLog log, size_t index) {
    return (index < array_count(log->topics)
            ? log->topics[index]
            : emptyTopic);
}

extern BRRlpData
logGetData (BREthereumLog log) {
    BRRlpData data;

    data.bytesCount = log->dataCount;
    data.bytes = malloc (data.bytesCount);
    memcpy (data.bytes, log->data, data.bytesCount);

    return data;
}

extern BREthereumBoolean
logMatchesAddress (BREthereumLog log,
                   BREthereumAddress address,
                   BREthereumBoolean topicsOnly) {
    int match = 0;
    size_t count = logGetTopicsCount(log);
    for (int i = 0; i < count; i++)
        match |= logTopicMatchesAddressBool(log->topics[i], address);

    return (ETHEREUM_BOOLEAN_IS_TRUE(topicsOnly)
            ? AS_ETHEREUM_BOOLEAN(match)
            : AS_ETHEREUM_BOOLEAN(match | ETHEREUM_BOOLEAN_IS_TRUE(logHasAddress(log, address))));

}

extern int
logExtractIncluded(BREthereumLog log,
                   BREthereumHash *blockHash,
                   uint64_t *blockNumber) {
    if (LOG_STATUS_INCLUDED != log->status.type)
        return 0;

    if (NULL != blockHash) *blockHash = log->status.u.included.blockHash;
    if (NULL != blockNumber) *blockNumber = log->status.u.included.blockNumber;

    return 1;
}

// Support BRSet
extern size_t
logHashValue (const void *l) {
    return hashSetValue(&((BREthereumLog) l)->hash);
}

// Support BRSet
extern int
logHashEqual (const void *l1, const void *l2) {
    return hashSetEqual(&((BREthereumLog) l1)->hash,
                        &((BREthereumLog) l2)->hash);
}

//
// Log Topics - RLP Encode/Decode
//
static BRRlpItem
logTopicsRlpEncodeItem (BREthereumLog log,
                        BRRlpCoder coder) {
    size_t itemsCount = array_count(log->topics);
    BRRlpItem items[itemsCount];

    for (int i = 0; i < itemsCount; i++)
        items[i] = logTopicRlpEncodeItem(log->topics[i], coder);

    return rlpEncodeListItems(coder, items, itemsCount);
}

static BREthereumLogTopic *
logTopicsRlpDecodeItem (BRRlpItem item,
                        BRRlpCoder coder) {
    size_t itemsCount = 0;
    const BRRlpItem *items = rlpDecodeList(coder, item, &itemsCount);

    BREthereumLogTopic *topics;
    array_new(topics, itemsCount);

    for (int i = 0; i < itemsCount; i++) {
        BREthereumLogTopic topic = logTopicRlpDecodeItem(items[i], coder);
        array_add(topics, topic);
    }

    return topics;
}

extern void
logRelease (BREthereumLog log) {
    array_free(log->topics);
    if (NULL != log->data) free (log->data);
    free (log);
}

extern void
logReleaseForSet (void *ignore, void *item) {
    logRelease((BREthereumLog) item);
}

extern BREthereumLog
logCopy (BREthereumLog log) {
    BRRlpCoder coder = rlpCoderCreate();
    BRRlpItem item = logRlpEncodeItem(log, coder);
    BREthereumLog copy = logRlpDecodeItem(item, coder);
    rlpCoderRelease(coder);
    return copy;
}

//
// Log - RLP Decode
//
extern BREthereumLog
logRlpDecodeItem (BRRlpItem item,
                  BRRlpCoder coder) {
    BREthereumLog log = (BREthereumLog) calloc (1, sizeof (struct BREthereumLogRecord));

    size_t itemsCount = 0;
    const BRRlpItem *items = rlpDecodeList(coder, item, &itemsCount);
    assert (3 == itemsCount);

    log->address = addressRlpDecode(items[0], coder);
    log->topics = logTopicsRlpDecodeItem (items[1], coder);

    BRRlpData data = rlpDecodeItemBytes(coder, items[2]);
    log->data = data.bytes;
    log->dataCount = data.bytesCount;

    return log;
}

extern BREthereumLog
logDecodeRLP (BRRlpData data) {
    BRRlpCoder coder = rlpCoderCreate();
    BRRlpItem item = rlpGetItem (coder, data);

    BREthereumLog log = logRlpDecodeItem(item, coder);

    rlpCoderRelease(coder);
    return log;
}

//
// Log - RLP Encode
//
extern BRRlpItem
logRlpEncodeItem(BREthereumLog log,
                 BRRlpCoder coder) {

    BRRlpItem items[3];

    items[0] = addressRlpEncode(log->address, coder);
    items[1] = logTopicsRlpEncodeItem(log, coder);
    items[2] = rlpEncodeItemBytes(coder, log->data, log->dataCount);

    return rlpEncodeListItems(coder, items, 3);
}

extern BRRlpData
logEncodeRLP (BREthereumLog log) {
    BRRlpData result;

    BRRlpCoder coder = rlpCoderCreate();
    BRRlpItem encoding = logRlpEncodeItem(log, coder);

    rlpDataExtract(coder, encoding, &result.bytes, &result.bytesCount);
    rlpCoderRelease(coder);

    return result;
}

/* Log (2) w/ LogTopic (3)
 ETH: LES-RECEIPTS:         L  2: [
 ETH: LES-RECEIPTS:           L  3: [
 ETH: LES-RECEIPTS:             I 20: 0x96477a1c968a0e64e53b7ed01d0d6e4a311945c2
 ETH: LES-RECEIPTS:             L  3: [
 ETH: LES-RECEIPTS:               I 32: 0x8c5be1e5ebec7d5bd14f71427d1e84f3dd0314c0f7b2291e5b200ac8c7c3b925
 ETH: LES-RECEIPTS:               I 32: 0x0000000000000000000000005c0f318407f37029f2a2b6b29468b79fbd178f2a
 ETH: LES-RECEIPTS:               I 32: 0x000000000000000000000000642ae78fafbb8032da552d619ad43f1d81e4dd7c
 ETH: LES-RECEIPTS:             ]
 ETH: LES-RECEIPTS:             I 32: 0x00000000000000000000000000000000000000000000000006f05b59d3b20000
 ETH: LES-RECEIPTS:           ]
 ETH: LES-RECEIPTS:           L  3: [
 ETH: LES-RECEIPTS:             I 20: 0xc66ea802717bfb9833400264dd12c2bceaa34a6d
 ETH: LES-RECEIPTS:             L  3: [
 ETH: LES-RECEIPTS:               I 32: 0x8c5be1e5ebec7d5bd14f71427d1e84f3dd0314c0f7b2291e5b200ac8c7c3b925
 ETH: LES-RECEIPTS:               I 32: 0x0000000000000000000000005c0f318407f37029f2a2b6b29468b79fbd178f2a
 ETH: LES-RECEIPTS:               I 32: 0x000000000000000000000000642ae78fafbb8032da552d619ad43f1d81e4dd7c
 ETH: LES-RECEIPTS:             ]
 ETH: LES-RECEIPTS:             I 32: 0x00000000000000000000000000000000000000000000000006f05b59d3b20000
 ETH: LES-RECEIPTS:           ]
 ETH: LES-RECEIPTS:         ]
*/
