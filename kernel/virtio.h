//
// virtio device definitions.
// for both the mmio interface, and virtio descriptors.
// only tested with qemu.
//
// the virtio spec:
// https://docs.oasis-open.org/virtio/virtio/v1.1/virtio-v1.1.pdf
//

// virtio mmio control registers, mapped starting at 0x10001000.
// from qemu virtio_mmio.h
#define VIRTIO_MMIO_MAGIC_VALUE         0x000 // 0x74726976
#define VIRTIO_MMIO_VERSION             0x004 // version 2 (1 is legacy)
#define VIRTIO_MMIO_DEVICE_ID           0x008 // device type; 1 is net, 2 is disk
#define VIRTIO_MMIO_VENDOR_ID           0x00c // 0x554d4551
#define VIRTIO_MMIO_DEVICE_FEATURES     0x010
#define VIRTIO_MMIO_DRIVER_FEATURES     0x020
#define VIRTIO_MMIO_QUEUE_SEL           0x030 // select queue, write-only
#define VIRTIO_MMIO_QUEUE_NUM_MAX       0x034 // max size of current queue, read-only
#define VIRTIO_MMIO_QUEUE_NUM           0x038 // size of current queue, write-only
#define VIRTIO_MMIO_QUEUE_READY         0x044 // ready bit
#define VIRTIO_MMIO_QUEUE_NOTIFY        0x050 // write-only
#define VIRTIO_MMIO_INTERRUPT_STATUS    0x060 // read-only
#define VIRTIO_MMIO_INTERRUPT_ACK       0x064 // write-only
#define VIRTIO_MMIO_STATUS              0x070 // read/write
#define VIRTIO_MMIO_QUEUE_DESC_LOW      0x080 // physical address for descriptor table, write-only
#define VIRTIO_MMIO_QUEUE_DESC_HIGH     0x084
#define VIRTIO_MMIO_DRIVER_DESC_LOW     0x090 // physical address for available ring, write-only
#define VIRTIO_MMIO_DRIVER_DESC_HIGH    0x094
#define VIRTIO_MMIO_DEVICE_DESC_LOW     0x0a0 // physical address for used ring, write-only
#define VIRTIO_MMIO_DEVICE_DESC_HIGH    0x0a4
#define VIRTIO_MMIO_CONFIG              0x100 // configuration space

// status register bits, from qemu virtio_config.h
#define VIRTIO_CONFIG_S_ACKNOWLEDGE     1
#define VIRTIO_CONFIG_S_DRIVER          2
#define VIRTIO_CONFIG_S_DRIVER_OK       4
#define VIRTIO_CONFIG_S_FEATURES_OK     8

// device feature bits
#define VIRTIO_BLK_F_RO              5  /* Disk is read-only */
#define VIRTIO_BLK_F_SCSI            7  /* Supports scsi command passthru */
#define VIRTIO_BLK_F_CONFIG_WCE     11  /* Writeback mode available in config */
#define VIRTIO_BLK_F_MQ             12  /* support more than one vq */
#define VIRTIO_F_ANY_LAYOUT         27
#define VIRTIO_RING_F_INDIRECT_DESC 28
#define VIRTIO_RING_F_EVENT_IDX     29

#define VIRTIO_NET_F_CSUM                 0
#define VIRTIO_NET_F_GUEST_CSUM           1
#define VIRTIO_NET_F_CTRL_GUEST_OFFLOADS  2
#define VIRTIO_NET_F_MTU                  3
#define VIRTIO_NET_F_MAC                  5
#define VIRTIO_NET_F_GUEST_TSO4           7
#define VIRTIO_NET_F_GUEST_TSO6           8
#define VIRTIO_NET_F_GUEST_ECN            9
#define VIRTIO_NET_F_GUEST_UFO           10
#define VIRTIO_NET_F_HOST_TSO4           11
#define VIRTIO_NET_F_HOST_TSO6           12
#define VIRTIO_NET_F_HOST_ECN            13
#define VIRTIO_NET_F_HOST_UFO            14
#define VIRTIO_NET_F_MRG_RXBUF           15
#define VIRTIO_NET_F_STATUS              16
#define VIRTIO_NET_F_CTRL_VQ             17
#define VIRTIO_NET_F_CTRL_RX             18
#define VIRTIO_NET_F_CTRL_VLAN           19
#define VIRTIO_NET_F_GUEST_ANNOUNCE      21
#define VIRTIO_NET_F_MQ                  22
#define VIRTIO_NET_F_CTRL_MAC_ADDR       23
#define VIRTIO_NET_F_RSC_EXT             61
#define VIRTIO_NET_F_STANDBY             62

// a single descriptor, from the spec.
struct virtq_desc {
  uint64 addr;
  uint32 len;
  uint16 flags;
  uint16 next;
};
#define VIRTQ_DESC_F_NEXT  1 // chained with another descriptor
#define VIRTQ_DESC_F_WRITE 2 // device writes (vs read)

// the (entire) avail ring, from the spec.
struct virtq_avail {
  uint16 flags; // always zero
  uint16 idx;   // driver will write ring[idx] next
  uint16 ring[]; // descriptor numbers of chain heads
};
#define VIRTQ_AVAIL_F_NO_INTERRUPT 1 // suppress interrupts

// one entry in the "used" ring, with which the
// device tells the driver about completed requests.
struct virtq_used_elem {
  uint32 id;   // index of start of completed descriptor chain
  uint32 len;
};

struct virtq_used {
  uint16 flags; // always zero
  volatile uint16 idx;   // device increments when it adds a ring[] entry
  struct virtq_used_elem ring[];
};

// these are specific to virtio block devices, e.g. disks,
// described in Section 5.2 of the spec.

// for disk ops
#define VIRTIO_BLK_T_IN  0 // read the disk
#define VIRTIO_BLK_T_OUT 1 // write the disk

// the format of the first descriptor in a disk request.
// to be followed by two more descriptors containing
// the block, and a one-byte status.
struct virtio_blk_req {
  uint32 type; // VIRTIO_BLK_T_IN or ..._OUT
  uint32 reserved;
  uint64 sector;
};

// these are specific to virtio net devices
// described in Section 5.1 of the spec.

// spec 5.1.4 Device configuration layout
struct virtio_net_config { 
  uint8 mac[6];                 // only valid if VIRTIO_NET_F_MAC is set
  uint16 status;                // only exists if VIRTIO_NET_F_STATUS is set
  uint16 max_virtqueue_pairs;   // only exists if VIRTIO_NET_F_MQ is set
  uint16 mtu;                   // only exists if VIRTIO_NET_F_MTU is set
};

// header for each packet
// spec 5.1.6 Device Operation
struct virtio_net_hdr {
#define VIRTIO_NET_HDR_F_NEEDS_CSUM 1
#define VIRTIO_NET_HDR_F_DATA_VALID 2
#define VIRTIO_NET_HDR_F_RSC_INFO   4
  uint8   flags; 
#define VIRTIO_NET_HDR_GSO_NONE     0
#define VIRTIO_NET_HDR_GSO_TCPV4    1
#define VIRTIO_NET_HDR_GSO_UDP      3
#define VIRTIO_NET_HDR_GSO_TCPV6    4
#define VIRTIO_NET_HDR_GSO_ECN   0x80
  uint8   gso_type; 
  uint16  hdr_len; 
  uint16  gso_size; 
  uint16  csum_start; 
  uint16  csum_offset; 
  uint16  num_buffers; 
};