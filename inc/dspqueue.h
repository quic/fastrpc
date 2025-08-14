// Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

/** @file
    Asynchronous DSP Packet Queue API.
*/

#ifndef DSPQUEUE_H
#define DSPQUEUE_H

#include <AEEStdDef.h>
#include <stdint.h>
#include <stdlib.h>

/** @defgroup dspqueue_consts Asynchronous DSP Packet Queue API Constants
 *  @{
 */

/** Infinite timeout */
#define DSPQUEUE_TIMEOUT_NONE 0xffffffff

/**
 * Packet flags. The flags are used as a bitfield in packet read/write
 * operations.
 */
enum dspqueue_packet_flags {
	DSPQUEUE_PACKET_FLAG_MESSAGE
	= 0x0001, /**< Packet contains a message */
	DSPQUEUE_PACKET_FLAG_BUFFERS
	= 0x0002, /**< Packet contains buffer references */
	DSPQUEUE_PACKET_FLAG_WAKEUP = 0x0004, /**< Early wakeup packet */
	DSPQUEUE_PACKET_FLAG_DRIVER_READY
	= 0x0008, /**< Packet is ready for driver consumption. Currently
	             unused. */
	DSPQUEUE_PACKET_FLAG_USER_READY
	= 0x0010, /**< Packet is ready for userspace library consumption */
	DSPQUEUE_PACKET_FLAG_RESERVED_ZERO = 0xffe0
};

/**
 * Buffer flags. The flags are used in dspqueue_buffer.flags as a bitfield.
 */
enum dspqueue_buffer_flags {
	/* 1 and 2 reserved */
	DSPQUEUE_BUFFER_FLAG_REF
	= 0x00000004, /**< Add a reference to a previously mapped buffer */
	DSPQUEUE_BUFFER_FLAG_DEREF = 0x00000008, /**< Remove a reference from a
	                                            previously mapped buffer */
	DSPQUEUE_BUFFER_FLAG_FLUSH_SENDER
	= 0x00000010, /**< Flush buffer from sender caches */
	DSPQUEUE_BUFFER_FLAG_INVALIDATE_SENDER
	= 0x00000020, /**< Invalidate buffer from sender caches */
	DSPQUEUE_BUFFER_FLAG_FLUSH_RECIPIENT
	= 0x00000040, /**< Flush buffer from recipient caches */
	DSPQUEUE_BUFFER_FLAG_INVALIDATE_RECIPIENT
	= 0x00000080, /**< Invalidate buffer from recipient caches */
	DSPQUEUE_BUFFER_FLAG_RESERVED_ZERO = 0xffffff00
};

/**
 * Statistics readable with dspqueue_get_stat()
 */
enum dspqueue_stat {
	DSPQUEUE_STAT_READ_QUEUE_PACKETS
	= 1, /**< Numbers of packets in the read queue */
	DSPQUEUE_STAT_READ_QUEUE_BYTES, /**< Number of bytes in the read queue
	                                 */
	DSPQUEUE_STAT_WRITE_QUEUE_PACKETS, /**< Number of packets in the write
	                                      queue */
	DSPQUEUE_STAT_WRITE_QUEUE_BYTES,   /**< Number of bytes in the write
	                                      queue */
	DSPQUEUE_STAT_EARLY_WAKEUP_WAIT_TIME, /**< Total accumulated early
	                                         wakeup wait time in
	                                         microseconds */
	DSPQUEUE_STAT_EARLY_WAKEUP_MISSES, /**< Number accumulated of packets
	                                      missed in the early wakeup loop
	                                    */
	DSPQUEUE_STAT_SIGNALING_PERF /**< Signaling performance; 0 or undefined
	                                indicates the first implementation,
	                                  DSPQUEUE_SIGNALING_PERF_REDUCED_SIGNALING
	                                or higher a version if reduced
	                                  signaling for polling clients. */
};

/* Request IDs to be used with "dspqueue_request" */
typedef enum {
	/*
	 * Create a multi-domain dspqueue i.e. a queue which has one or more
	 * DSP endpoints. Any packet written to the queue will be delivered
	 * to each endpoint and a signal will be sent to all waiting threads
	 * on each endpoint.
	 */
	DSPQUEUE_CREATE,
} dspqueue_request_req_id;

/** Signaling performance level: Reduced signaling for polling clients. */
#define DSPQUEUE_SIGNALING_PERF_REDUCED_SIGNALING 100

/** Signaling performance level: Optimized signaling for all clients. */
#define DSPQUEUE_SIGNALING_PERF_OPTIMIZED_SIGNALING 1000

/** @}
 */

/** @defgroup dspqueue_types Asynchronous DSP Packet Queue API Data Types
 *  @{
 */

struct dspqueue;
typedef struct dspqueue *dspqueue_t; /**< Queue handle */

/**
 * Buffer reference in a packet.
 * The buffer must already be mapped to the DSP using the same file descriptor.
 * The subsection of the buffer as specified by #offset and #size must fit
 * entirely within the mapped buffer.
 * Note that buffer references are tracked based on the buffer file descriptor,
 * and taking/releasing a reference to a buffer applies to the entire buffer as
 * mapped to the DSP, not just the subsection specified.
 */
struct dspqueue_buffer {
	uint32_t fd;   /**< Buffer file descriptor */
	uint32_t size; /**< Buffer size in bytes. The client can set this field
	                    to zero when writing packets; in this case the
	                    framework will set the field to the size of the
	                    buffer as mapped. */
	uint32_t offset; /**< Offset within the buffer in bytes as allocated
	                    and mapped.     The virtual address #ptr includes
	                    the offset */
	uint32_t flags;  /**< Buffer flags, see enum #dspqueue_buffer_flags */
	union {
		void *ptr; /**< Buffer virtual address; NULL if not mapped in
		              the local context */
		uint64_t address;
	};
};

/**
 * Callback function type for all queue callbacks
 *
 * @param queue Queue handle from dspqueue_create() / dspqueue_import()
 * @param error Error code
 * @param context Client-provided context pointer
 */
typedef void (*dspqueue_callback_t)(dspqueue_t queue, AEEResult error,
                                    void *context);

/* Struct to be used with DSPQUEUE_CREATE request */
typedef struct dspqueue_create_req {
	/* [in]: Fastrpc multi-domain context */
	uint64_t ctx;

	/* [in]: Queue creation flags (unused for now) */
	uint64_t flags;

	/*
	 * [in]: Total request queue memory size in bytes;
	 * use 0 for system default
	 */
	uint32_t req_queue_size;

	/*
	 * [in]: Total response queue memory size in bytes;
	 * use 0 for system default.
	 * For each domain, one response queue of this size will be created
	 * and mapped i.e. if there are N domains provided, there will be N
	 * response queues created.
	 */
	uint32_t resp_queue_size;

	/* [in] Queue priority (unused for now) */
	uint32_t priority;

	/*
	 * [in]: Callback function called when there are new packets to read.
	 * When there are response packets from multi-domains on the queue,
	 * this callback can be called concurrently from multiple threads.
	 * Client is expected to consume each available response packet.
	 */
	dspqueue_callback_t packet_callback;

	/*
	 * [in]: Callback function called on unrecoverable errors.
	 * NULL to disable.
	 */
	dspqueue_callback_t error_callback;

	/* [in]: Context pointer for callback functions */
	void *callback_context;

	/* [out]: Queue handle */
	dspqueue_t queue;

	/*
	 * [out]: Queue ID array. Needs to be allocated by caller.
	 * Array will be populated with queue IDs of each domain.
	 * Size of array should be same as number of domains on which
	 * multi-domain context was created.
	 */
	uint64_t *ids;

	/* [in] Size of queue IDs array (must be same as number of domains) */
	uint32_t num_ids;
} dspqueue_create_req;

/* Request payload */
typedef struct dspqueue_request_payload {
	/* Request id */
	dspqueue_request_req_id id;
	/* Request payload */
	union {
		dspqueue_create_req create;
	};
} dspqueue_request_payload;

/** @}
 */

#ifdef __cplusplus
extern "C" {
#endif

/** @defgroup dspqueue_funcs Asynchronous DSP Packet Queue API
 * Functions
 *  @{
 */

/**
 * Create a new queue to communicate with the DSP. Queues can only be
 * created on the host CPU.
 *
 * This function cannot be used to create multi-domain queue.
 * Refer 'dspqueue_request' for that.
 *
 * @param [in] domain DSP to communicate with (CDSP_DOMAIN_ID in
 * remote.h for cDSP)
 * @param [in] flags Queue creation flags
 * @param [in] req_queue_size Total request queue memory size in bytes;
 * use 0 for system default
 * @param [in] resp_queue_size Total response queue memory size in
 * bytes; use 0 for system default
 * @param [in] packet_callback Callback function called when there are
 * new packets to read. The call will be done in a different thread's
 * context. NULL to disable the callback. Clients cannot use blocking
 * read calls if a packet callback has been set.
 * @param [in] error_callback Callback function called on unrecoverable
 * errors. NULL to disable.
 * @param [in] callback_context Context pointer for callback functions
 * @param [out] queue Queue handle
 *
 * @return 0 on success, error code on failure.
 *         - AEE_ENOMEMORY: Not enough memory available
 *         - AEE_EUNSUPPORTED: Message queue not supported on the given
 * DSP
 *         - AEE_EBADPARM: Bad parameters, e.g. Invalid domain (use
 * CDSP_DOMAIN_ID for cDSP)
 *         - AEE_ERPC: Internal RPC error, e.g. Queue list corrupt
 */
AEEResult dspqueue_create(int domain, uint32_t flags, uint32_t req_queue_size,
                          uint32_t resp_queue_size,
                          dspqueue_callback_t packet_callback,
                          dspqueue_callback_t error_callback,
                          void *callback_context, dspqueue_t *queue);

/**
 * Close a queue and free all memory associated with it. The
 * function can be called on the host CPU with queue handles from
 * dspqueue_create() or on the DSP with handles from
 * dspqueue_import().
 *
 * This function can be called on both single-domain and multi-domain
 * queues.
 *
 * @param [in] queue Queue handle from dsp_queue_create() from
 * dsp_queue_import().
 *
 * @return 0 on success, error code on failure.
 *         - AEE_ERPC: Internal RPC error, e.g. The queue is open on
 * the DSP when attempting to close it on the host CPU
 */
AEEResult dspqueue_close(dspqueue_t queue);

/**
 * Export a queue to the DSP. The CPU-side client calls this function,
 * passes the ID to the DSP, which can then call dspqueue_import() to
 * access the queue.
 *
 * This function is not required to be called on multi-domain queues.
 *
 * @param [in] queue Queue handle from dspqueue_create()
 * @param [out] queue_id Queue ID
 *
 * @return 0 on success, error code on failure.
 */
AEEResult dspqueue_export(dspqueue_t queue, uint64_t *queue_id);

/**
 * Import a queue on the DSP based on an ID passed in from the host
 * CPU. The DSP client can use the returned queue handle to access the
 * queue and communicate with its host CPU counterpart.
 *
 * @param [in] queue_id Queue ID from dspqueue_export().
 * @param [in] packet_callback Callback function called when there are
 * new packets to read. The call will be done in a different thread's
 * context. NULL to disable the callback.
 * @param [in] error_callback Callback function called on unrecoverable
 * errors. NULL to disable.
 * @param [in] callback_context Context pointer fo callback functions
 * @param [out] queue Queue handle
 *
 * @return 0 on success, error code on failure.
 *         - AEE_EITEMBUSY: The queue has already been imported
 *         - AEE_EQURTTHREADCREATE: Unable to create callback thread;
 * the system may have reached its thread limit.
 *         - AEE_EBADSTATE: Bad internal state
 */
AEEResult dspqueue_import(uint64_t queue_id,
                          dspqueue_callback_t packet_callback,
                          dspqueue_callback_t error_callback,
                          void *callback_context, dspqueue_t *queue);

/**
 * Make dspqueue related requests - like creation of multi-domain queue
 *
 * @param [in] req : Request payload
 *
 * @return 0 on success, error code on failure.
 *			- AEE_ENOMEMORY    : Not enough memory
 *available
 *			- AEE_EUNSUPPORTED : Not supported on given
 *domains
 *			- AEE_EBADPARM     : Bad parameters, e.g.
 *invalid context
 *			- AEE_ERPC         : Internal RPC error
 */
int dspqueue_request(dspqueue_request_payload *req);

/**
 * Write a packet to a queue. This variant of the function will not
 * block, and will instead return AEE_EWOULDBLOCK if the queue does not
 * have enough space for the packet.
 *
 * With this function the client can pass separate pointers to the
 * buffer references and message to include in the packet and the
 * library copies the contents directly to the queue.
 *
 * When this is called on a multi-domain queue, the packet will be
 * shared with all remote domains the queue was created on. If any of
 * the domains is unable to receive the packet, it means the queue is
 * in a bad-state and is no longer usable. Client is expected to close
 * the queue and reopen a new one.
 *
 * @param [in] queue Queue handle from dspqueue_create() or
 * dspqueue_import()
 * @param [in] flags Packet flags. See enum #dspqueue_packet_flags
 * @param [in] num_buffers Number of buffer references to insert to the
 * packet; zero if there are no buffer references
 * @param [in] buffers Pointer to buffer references
 * @param [in] message_length Message length in bytes;
 *                       zero if the packet contains no message
 * @param [in] message Pointer to packet message
 *
 * @return 0 on success, error code on failure.
 *         - AEE_EWOULDBLOCK: The queue is full
 *         - AEE_EBADPARM: Bad parameters, e.g. buffers is NULL when
 * num_buffers > 0
 *         - AEE_ENOSUCHMAP: Attempt to refer to an unmapped buffer.
 * Buffers must be mapped to the DSP with fastrpc_mmap() before they
 * can be used in queue packets.
 *         - AEE_EBADSTATE: Queue is in bad-state and can no longer be
 * used
 */
AEEResult dspqueue_write_noblock(dspqueue_t queue, uint32_t flags,
                                 uint32_t num_buffers,
                                 struct dspqueue_buffer *buffers,
                                 uint32_t message_length,
                                 const uint8_t *message);

/**
 * Write a packet to a queue. If the queue is full this function will
 * block until space becomes available or the request times out.
 *
 * With this function the client can pass separate pointers to the
 * buffer references and message to include in the packet and the
 * library copies the contents directly to the queue.
 *
 * When this is called on a multi-domain queue, the packet will be
 * shared with all remote domains the queue was created on. This call
 * will block (for specified timeout or indefinitely) until the packet
 * is shared with all domains. If any of the domains is unable to
 * receive the packet, it means the queue is in a bad-state and is no
 * longer usable. Client is expected to close the queue and reopen a
 * new one.
 *
 * @param [in] queue Queue handle from dspqueue_create() or
 * dspqueue_import()
 * @param [in] flags Packet flags. See enum #dspqueue_packet_flags
 * @param [in] num_buffers Number of buffer references to insert to the
 * packet; zero if there are no buffer references
 * @param [in] buffers Pointer to buffer references
 * @param [in] message_length Message length in bytes;
 *                       zero if the packet contains no message
 * @param [in] message Pointer to packet message
 * @param [in] timeout_us Timeout in microseconds; use
 * DSPQUEUE_TIMEOUT_NONE to block indefinitely until a space is
 * available or zero for non-blocking behavior.
 *
 * @return 0 on success, error code on failure.
 *         - AEE_EBADPARM: Bad parameters, e.g. buffers is NULL when
 * num_buffers > 0
 *         - AEE_ENOSUCHMAP: Attempt to refer to an unmapped buffer.
 * Buffers must be mapped to the DSP with fastrpc_mmap() before they
 * can be used in queue packets.
 *         - AEE_EEXPIRED: Request timed out
 *         - AEE_EINTERRUPTED: The request was canceled
 *         - AEE_EBADSTATE: Queue is in bad-state and can no longer be
 * used
 */
AEEResult dspqueue_write(dspqueue_t queue, uint32_t flags,
                         uint32_t num_buffers, struct dspqueue_buffer *buffers,
                         uint32_t message_length, const uint8_t *message,
                         uint32_t timeout_us);

/**
 * Read a packet from a queue. This variant of the function will not
 * block, and will instead return AEE_EWOULDBLOCK if the queue does not
 * have enough space for the packet.
 *
 * This function will read packet contents directly into
 * client-provided buffers. The buffers must be large enough to fit
 * contents from the packet or the call will fail.
 *
 * When this is called on a multi-domain queue, it will return the
 * response packet from the first domain where it finds one. If
 * multiple domains have posted a response to the multi-domain queue,
 * the client is expected to call this function as many times to
 * consume the response packet from all domains.
 *
 * @param [in] queue Queue handle from dspqueue_create() or
 * dspqueue_import()
 * @param [out] flags Packet flags. See enum #dspqueue_packet_flags
 * @param [in] max_buffers The maximum number of buffer references that
 * can fit in the "buffers" parameter
 * @param [out] num_buffers The number of buffer references in the
 * packet
 * @param [out] buffers Buffer reference data from the packet
 * @param [in] max_message_length Maximum message length that can fit
 * in the "message" parameter
 * @param [out] message_length Message length in bytes
 * @param [out] message Packet message
 *
 * @return 0 on success, error code on failure.
 *         - AEE_ENOSUCHMAP: The packet refers to an unmapped buffer.
 * Buffers must be mapped to the DSP with fastrpc_mmap() before they
 * can be used in queue packets.
 *         - AEE_EWOULDBLOCK: The queue is empty; try again later
 *         - AEE_EBADITEM: The queue contains a corrupted packet.
 * Internal error.
 */
AEEResult dspqueue_read_noblock(dspqueue_t queue, uint32_t *flags,
                                uint32_t max_buffers, uint32_t *num_buffers,
                                struct dspqueue_buffer *buffers,
                                uint32_t max_message_length,
                                uint32_t *message_length, uint8_t *message);

/**
 * Read a packet from a queue. If the queue is empty this function
 * will block until a packet is available or the request times out.
 * The queue must not have a packet callback set.
 *
 * This function will read packet contents directly into
 * client-provided buffers. The buffers must be large enough to fit
 * contents from the packet or the call will fail.
 *
 * This function is currently not supported on multi-domain queues.
 *
 * @param [in] queue Queue handle from dspqueue_create() or
 * dspqueue_import()
 * @param [out] flags Packet flags. See enum #dspqueue_packet_flags
 * @param [in] max_buffers The maximum number of buffer references that
 * can fit in the "buffers" parameter
 * @param [out] num_buffers The number of buffer references in the
 * packet
 * @param [out] buffers Buffer reference data from the packet
 * @param [in] max_message_length Maximum message length that can fit
 * in the "message" parameter
 * @param [out] message_length Message length in bytes
 * @param [out] message Packet message
 * @param [in] timeout_us Timeout in microseconds; use
 * DSPQUEUE_TIMEOUT_NONE to block indefinitely until a packet is
 * available or zero for non-blocking behavior.
 *
 * @return 0 on success, error code on failure.
 *         - AEE_ENOSUCHMAP: The packet refers to an unmapped buffer.
 * Buffers must be mapped to the DSP with fastrpc_mmap() before they
 * can be used in queue packets.
 *         - AEE_EBADITEM: The queue contains a corrupted packet.
 * Internal error.
 *         - AEE_EEXPIRED: Request timed out
 *         - AEE_EINTERRUPTED: The request was canceled
 */
AEEResult dspqueue_read(dspqueue_t queue, uint32_t *flags,
                        uint32_t max_buffers, uint32_t *num_buffers,
                        struct dspqueue_buffer *buffers,
                        uint32_t max_message_length, uint32_t *message_length,
                        uint8_t *message, uint32_t timeout_us);

/**
 * Retrieve information for the next packet if available, without
 * reading it from the queue and advancing the read pointer. This
 * function will not block, but will instead return an error if the
 * queue is empty.
 *
 * When this is called on a multi-domain queue, it will return the
 * response packet info from the first domain where it finds one. If
 * multiple domains have posted a response to the multi-domain queue,
 * the client is expected to consume a peeked packet first before
 * attempting to peek the next available packet from any of the
 * domains.
 *
 * @param [in] queue Queue handle from dspqueue_create() or
 * dspqueue_import().
 * @param [out] flags Packet flags. See enum #dspqueue_packet_flags
 * @param [out] num_buffers Number of buffer references in packet
 * @param [out] message_length Packet message length in bytes
 *
 * @return 0 on success, error code on failure.
 *         - AEE_EWOULDBLOCK: The queue is empty; try again later
 *         - AEE_EBADITEM: The queue contains a corrupted packet.
 * Internal error.
 */
AEEResult dspqueue_peek_noblock(dspqueue_t queue, uint32_t *flags,
                                uint32_t *num_buffers,
                                uint32_t *message_length);

/**
 * Retrieve information for the next packet, without reading it from
 * the queue and advancing the read pointer. If the queue is empty this
 * function will block until a packet is available or the request
 * times out.
 *
 * This function is currently not supported on multi-domain queues.
 *
 * @param [in] queue Queue handle from dspqueue_create() or
 * dspqueue_import().
 * @param [out] flags Packet flags. See enum #dspqueue_packet_flags
 * @param [out] num_buffers Number of buffer references in packet
 * @param [out] message_length Packet message length in bytes
 * @param [out] timeout_us Timeout in microseconds; use
 * DSPQUEUE_TIMEOUT_NONE to block indefinitely until a packet is
 * available or zero for non-blocking behavior.
 *
 * @return 0 on success, error code on failure.
 *         - AEE_EEXPIRED: Request timed out
 *         - AEE_EINTERRUPTED: The request was canceled
 *         - AEE_EBADITEM: The queue contains a corrupted packet.
 * Internal error.
 */
AEEResult dspqueue_peek(dspqueue_t queue, uint32_t *flags,
                        uint32_t *num_buffers, uint32_t *message_length,
                        uint32_t timeout_us);

/**
 * Write an early wakeup packet to the queue. Early wakeup packets are
 * used to bring the recipient out of a low-power state in anticipation
 * of a real message packet being availble shortly, and are typically
 * used from the DSP to signal that an operation is almost complete.
 *
 * This function will return immediately if the queue is full. There is
 * no blocking variant of this function; if the queue is full the other
 * endpoint should already be processing data and an early wakeup would
 * not be useful.
 *
 * When this function is called on a multi-domain queue, early wakeup
 * is done on all the domains that the queue was created on.
 *
 * @param [in] queue Queue handle from dspqueue_create() or
 * dspqueue_import()
 * @param [in] wakeup_delay Wakeup time in microseconds; this indicates
 * how soon the real message packet should be available. Zero if not
 * known. The recipient can use this information to determine how to
 *                     wait for the packet.
 * @param [in] packet_flags Flags for the upcoming packet if known.
 *                    The recipient can use this information to
 * determine how to wait for the packet. See enum
 * #dspqueue_packet_flags
 *
 * @return 0 on success, error code on failure.
 *         - AEE_EWOULDBLOCK: The queue is full
 */
AEEResult dspqueue_write_early_wakeup_noblock(dspqueue_t queue,
                                              uint32_t wakeup_delay,
                                              uint32_t packet_flags);

/**
 * Retrieve statistics from a queue. Statistics are relative to the
 * queue as viewed from the current endpoint (e.g. "read queue" refers
 * to the queue as being read by the current endpoint).
 *
 * Reading an accumulating statistic (such as early wakeup wait time)
 * will reset it to zero.
 *
 * Note that statistics values are only valid at the time when they're
 * read.  By the time this function returns the values may have
 * changed due to actions from another thread or the other queue
 * endpoint.
 *
 * This function is currently not supported on multi-domain queues.
 *
 * @param [in] queue Queue handle from dspqueue_create() or
 * dspqueue_import()
 * @param [in] stat Statistic to read, see enum dspqueue_stat
 * @param [out] value Statistic value. Reading a statistic will reset
 * it to zero
 *
 * @return 0 on success, error code on failure.
 *         - AEE_EBADPARM: Invalid statistic
 */

AEEResult dspqueue_get_stat(dspqueue_t queue, enum dspqueue_stat stat,
                            uint64_t *value);

/** @}
 */

#ifdef __cplusplus
}
#endif

#endif // DSPQUEUE_H
