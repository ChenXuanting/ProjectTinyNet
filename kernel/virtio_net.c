#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "memlayout.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "virtio.h"

#define R(r) ((volatile uint32 *)(VIRTIO1 + (r)))

// this many virtio descriptors.
// must be a power of two.
#define NUM 32

struct virtqueue {
    // The descriptor table tells the device where to read and write
    // individual operations.
    struct virtq_desc *desc;
    // The available ring is where the driver writes descriptor numbers
    // that the driver would like the device to process (just the head
    // of each chain). The ring has NUM elements.
    struct virtq_avail *avail;
    // The used ring is where the device writes descriptor numbers that
    // the device has finished processing (just the head of each chain).
    // The ring has NUM elements.
    struct virtq_used *used;

    // our own book-keeping.
    char free[2*NUM];   // is a descriptor free?
    uint16 used_idx;    // we've looked this far in used->ring.

    // packet headers
    // one-for-one with descriptors, for convenience.
    struct virtio_net_hdr ops[NUM];
};

void mmio_virtq_init(struct virtqueue *q, int qidx) {
    /* 
     * MMIO-specific initialization
     * check spec 4.2.3.2 Virtqueue Configuration
     */
    
    // 1. select a queue by writing to queuesel
    *R(VIRTIO_MMIO_QUEUE_SEL) = qidx;
    
    // 2. check if the queue is already in use
    if(*R(VIRTIO_MMIO_QUEUE_READY))
        panic("virtq_init: queue already in use");
    
    // 3. check if the queue is available (queue-num-max != 0)
    uint32 max = *R(VIRTIO_MMIO_QUEUE_NUM_MAX);
    if(max == 0) panic("virtq_init: queue not available");
    if(max < NUM) panic("virtq_init: queue too short");

    // 4. allocate and zero the queue memory
    q->desc = (struct virtq_desc *)kalloc();
    q->avail = (struct virtq_avail *)kalloc();
    q->used = (struct virtq_used *)kalloc();
    
    if (!q->desc || !q->avail || !q->used)
        panic("virtq_init: kalloc failed");

    memset(q->desc, 0, PGSIZE);
    memset(q->avail, 0, PGSIZE);
    memset(q->used, 0, PGSIZE);

    // 5. notify the device about the queue size
    *R(VIRTIO_MMIO_QUEUE_NUM) = NUM;

    // 6. write PA of three parts of the queue to the device
    *R(VIRTIO_MMIO_QUEUE_DESC_LOW)   = (uint64)q->desc;
    *R(VIRTIO_MMIO_QUEUE_DESC_HIGH)  = (uint64)q->desc >> 32;
    *R(VIRTIO_MMIO_DRIVER_DESC_LOW)  = (uint64)q->avail;
    *R(VIRTIO_MMIO_DRIVER_DESC_HIGH) = (uint64)q->avail >> 32;
    *R(VIRTIO_MMIO_DEVICE_DESC_LOW)  = (uint64)q->used;
    *R(VIRTIO_MMIO_DEVICE_DESC_HIGH) = (uint64)q->used >> 32;

    // 7. tell the device that the queue is ready
    *R(VIRTIO_MMIO_QUEUE_READY) = 1;


    /* 
     * initialize book-keeping 
     */
    for(int i = 0; i < 2*NUM; i++)
        q->free[i] = 1;
    q->used_idx = 0;
}

struct net {
    struct virtqueue rx;
    struct virtqueue tx;
    void  *send_buf[NUM];
    void  *recv_buf[NUM];
    struct spinlock vnet_lock;
} net;

static void 
fill_rx(int i) {
    struct virtio_net_hdr *hdr = &net.rx.ops[i];

    // header is populated by the device
    // we don't fill it here

    net.rx.desc[2*i].addr = (uint64)hdr;
    net.rx.desc[2*i].len = sizeof(struct virtio_net_hdr);
    net.rx.desc[2*i].flags = VIRTQ_DESC_F_WRITE | VIRTQ_DESC_F_NEXT;
    net.rx.desc[2*i].next = 2*i+1;

    net.rx.desc[2*i+1].addr = (uint64)net.recv_buf[i];
    net.rx.desc[2*i+1].len = PGSIZE;
    net.rx.desc[2*i+1].flags = VIRTQ_DESC_F_WRITE;  // device writes to this buffer
    net.rx.desc[2*i+1].next = 0;               // VIRTQ_DESC_F_NEXT not set: no chaining

    int avail = net.rx.avail->idx % NUM;
    net.rx.avail->ring[avail] = 2*i;
    __sync_synchronize();
    net.rx.avail->idx++;

    // notify the device
    *R(VIRTIO_MMIO_QUEUE_NOTIFY) = 0;  // queue number of RX
}

/* initialize the NIC and store the MAC address */
void virtio_net_init(void *mac) {
    uint32 status = 0;

    initlock(&net.vnet_lock, "virtio_net");

    /*
     * MMIO-specific checking.
     * spec 4.2.3.1.1 Driver Requirements: Device Initialization
     */
    if (*R(VIRTIO_MMIO_MAGIC_VALUE) != 0x74726976 ||
        *R(VIRTIO_MMIO_VERSION) != 2 ||
        *R(VIRTIO_MMIO_DEVICE_ID) != 1 ||  // 1 for network device
        *R(VIRTIO_MMIO_VENDOR_ID) != 0x554d4551){
        panic("could not find virtio net");
    }

    /*
     * General initialization.
     * spec 3.1.1 Driver Requirements: Device Initialization
     */

    // Reset the device.
    *R(VIRTIO_MMIO_STATUS) = status;

    // Set the ACKNOWLEDGE bit.
    status |= VIRTIO_CONFIG_S_ACKNOWLEDGE;
    *R(VIRTIO_MMIO_STATUS) = status;

    // Set the DRIVER bit.
    status |= VIRTIO_CONFIG_S_DRIVER;
    *R(VIRTIO_MMIO_STATUS) = status;

    // Negotiate features.
    // spec 5.1.3 Feature bits
    uint64 features = *R(VIRTIO_MMIO_DEVICE_FEATURES);
    if (!(features & (1 << VIRTIO_NET_F_MAC)) || 
        !(features & (1 << VIRTIO_NET_F_MRG_RXBUF)))
            panic("virtio_net_init: device does not support MAC or MRG_RXBUF");

    // !! enable VIRTIO_NET_F_CSUM (0) 
    // Device (not driver) handles packets with partial checksum. 
    // If not negotiated, the driver SHOULD supply a fully checksummed packet to the device.
    features &= ~(1 << VIRTIO_NET_F_CSUM);

    // !! disable VIRTIO_NET_F_GUEST_CSUM (1)
    // If not negotiated, the device SHOULD supply a fully checksummed packet to the driver.
    features &= ~(1 << VIRTIO_NET_F_GUEST_CSUM);

    features &= ~(1 << VIRTIO_NET_F_GUEST_TSO4);
    features &= ~(1 << VIRTIO_NET_F_GUEST_TSO6);
    features &= ~(1 << VIRTIO_NET_F_GUEST_ECN);
    features &= ~(1 << VIRTIO_NET_F_GUEST_UFO);
    
    features &= ~(1 << VIRTIO_NET_F_HOST_TSO4);
    features &= ~(1 << VIRTIO_NET_F_HOST_TSO6);
    features &= ~(1 << VIRTIO_NET_F_HOST_ECN);
    features &= ~(1 << VIRTIO_NET_F_HOST_UFO);
    
    features &= ~(1L << VIRTIO_NET_F_RSC_EXT);

    features &= ~(1 << VIRTIO_NET_F_CTRL_VQ);
    features &= ~(1 << VIRTIO_NET_F_CTRL_RX);
    features &= ~(1 << VIRTIO_NET_F_CTRL_VLAN);

    features &= ~(1 << VIRTIO_NET_F_MQ);

    *R(VIRTIO_MMIO_DRIVER_FEATURES) = features;

    // Tell device that feature negotiation is complete.
    status |= VIRTIO_CONFIG_S_FEATURES_OK;
    *R(VIRTIO_MMIO_STATUS) = status;

    // Ensure the FEATURES_OK bit is set.
    status = *R(VIRTIO_MMIO_STATUS);
    if(!(status & VIRTIO_CONFIG_S_FEATURES_OK))
        panic("virtio net FEATURES_OK unset");
    
    /*
     * Device-specific initialization.
     * spec 5.1.5 Device Initialization (network device)
     */
    
    // 1. identify and initialize the virtqueues
    // queue idx: spec 5.1.2 Virtqueues
    mmio_virtq_init(&net.rx, 0);
    mmio_virtq_init(&net.tx, 1);
    
    for (int i = 0; i < NUM; i++) {
        net.send_buf[i] = kalloc();
        if (!net.send_buf[i])
            panic("virtio_net_init: kalloc failed");
        memset(net.send_buf[i], 0, PGSIZE);

        net.recv_buf[i] = kalloc();
        if (!net.recv_buf[i])
            panic("virtio_net_init: kalloc failed");
        memset(net.recv_buf[i], 0, PGSIZE);
    }

    // 2. fill receive queue with buffers
    // 5.1.6.3 Setting Up Receive Buffers
    // 2.6.5 The Virtqueue Descriptor Table
    for (int i = 0; i < NUM; i++)
        fill_rx(i);

    // 3. read and store the MAC address
    // spec 2.4.1 Driver Requirements: Device Configuration Space
    struct virtio_net_config *cfg = (struct virtio_net_config *)R(VIRTIO_MMIO_CONFIG);
    uint8 before, after;
    for (int i = 0; i < 6; i++) {
        do {
            before = cfg->mac[i];
            after = cfg->mac[i];
        } while (after != before);
        ((uint8 *)mac)[i] = before;
    }

    // Tell device we're completely ready.
    status |= VIRTIO_CONFIG_S_DRIVER_OK;
    *R(VIRTIO_MMIO_STATUS) = status;
}

// find a free descriptor, mark it non-free, return its index.
static int
alloc_desc(struct virtqueue *q)
{
  for(int i = 0; i < NUM; i++){
    if(q->free[i]){
      q->free[i] = 0;
      return i;
    }
  }
  return -1;
}

// mark a descriptor as free.
static void
free_desc(struct virtqueue *q, int i)
{
  if(i >= NUM)
    panic("free_desc: out of range");
  if(q->free[i])
    panic("free_desc: double free");
  q->desc[i].addr = 0;
  q->free[i] = 1;
  wakeup(&q->free[0]);  // wake up potential waiters
}

/* send data; return 0 on success */
// spec 5.1.6.2 Packet Transmission
int virtio_net_send(const void *data, int len) {
    acquire(&net.vnet_lock);

    // if the available ring is full, drop the packet
    // avail->idx and used->idx are only increased (no modulo)
    if (net.tx.avail->idx - net.tx.used->idx == NUM) {
        release(&net.vnet_lock);
        return -1;
    }

    // allocate one descriptor for header + data
    int idx = 0;
    while (1) {
        if ((idx = alloc_desc(&net.tx)) >= 0) 
            break;
        sleep(&net.tx.free[0], &net.vnet_lock);
    }

    // get preallocated buffer by idx
    void *buf = net.send_buf[idx];

    // fill in the header fields
    struct virtio_net_hdr *hdr = buf;
    hdr->flags = 0;             // assume the packet is completely checksummed
    hdr->csum_start = 0;        // unused
    hdr->csum_offset = 0;       // unused
    hdr->gso_type = VIRTIO_NET_HDR_GSO_NONE;
    hdr->hdr_len = 0;           // unused
    hdr->gso_size = 0;          // unused
    hdr->num_buffers = 0;       // driver must set num_buffers to 0

    // copy user data to the payload area
    void *payload = (void *)hdr + sizeof(struct virtio_net_hdr);
    memmove(payload, data, len);

    // fill in the fields of the descriptor
    net.tx.desc[idx].addr = (uint64)hdr;
    net.tx.desc[idx].len = sizeof(struct virtio_net_hdr) + len;
    net.tx.desc[idx].flags = 0;     // read-only
    net.tx.desc[idx].next = 0;      // device only reads from this buffer

    // update the available ring
    int avail = net.tx.avail->idx % NUM;
    net.tx.avail->ring[avail] = idx;
    __sync_synchronize();  // TODO: why?
    net.tx.avail->idx++;

    // notify the device
    *R(VIRTIO_MMIO_QUEUE_NOTIFY) = 1;  // queue number of TX

    // if called during initialization, wait for the device to process the packet
    if (myproc() == 0) {
        // should not sleep because interrupt is disabled
        // user->idx is incremented by the device
        while ((net.tx.used->idx % NUM) == (net.tx.used_idx % NUM))
            ;

        net.tx.used_idx = (net.tx.used_idx + 1) % NUM;
    }

    release(&net.vnet_lock);
    
    return 0;
}

/* receive data; return the number of bytes received */
// spec 5.1.6.4 Processing of Incoming Packets
int virtio_net_recv(void *data, int len) {
    acquire(&net.vnet_lock);

    // if called during initialization (DHCP-ARP), return immediately
    // TODO: not sure if this is the correct way to handle this, but it works
    if (myproc() == 0 && (net.rx.used->idx % NUM) == (net.rx.used_idx % NUM)) {
        release(&net.vnet_lock);
        return 0;
    }

    // wait for the device if the ring is empty
    while ((net.rx.used->idx % NUM) == (net.rx.used_idx % NUM)) {
        sleep(&net.rx, &net.vnet_lock);
    }

    // get received packet
    int hdr_idx, data_idx, data_len;
    struct virtq_used_elem *used;
    void *data_ptr;

    used = &net.rx.used->ring[net.rx.used_idx];
    hdr_idx = used->id;                                     // index of the header descriptor
    data_idx = net.rx.desc[hdr_idx].next;                   // index of the data descriptor
    data_ptr = (void *)net.rx.desc[data_idx].addr;          // address of the data
    data_len = used->len - sizeof(struct virtio_net_hdr);   // length of the data

    // copy the data to the user buffer
    memmove(data, data_ptr, data_len);

    // update bookkeeping info
    net.rx.used_idx = (net.rx.used_idx + 1) % NUM;

    // refill RX and notify the device
    // reuse the descriptor chain, no need to free and allocate it
    fill_rx(hdr_idx/2);

    release(&net.vnet_lock);
    
    return data_len;
}

// will be called in trap.c devintr()
void
virtio_net_intr(void)
{
    printf("virtio_net_intr\n");
    acquire(&net.vnet_lock);

    // only handle Used Buffer Notification (0x1) for now
    if (!(*R(VIRTIO_MMIO_INTERRUPT_STATUS) & 0x1)) {
        panic("virtio_net_intr: Configuration Change Notification");
    }

    // incoming packet: wake up potential waiters
    if ((net.rx.used->idx % NUM) != (net.rx.used_idx % NUM)) {
        // descriptor chain is freed in virtio_net_recv()
        wakeup(&net.rx);
    }

    // outgoing packet: free descriptors and do some bookkeeping
    if ((net.tx.used->idx % NUM) != (net.tx.used_idx % NUM)) {
        free_desc(&net.tx, net.tx.used->ring[net.tx.used_idx].id);
        net.tx.used_idx = (net.tx.used_idx + 1) % NUM;
    }

    // acknowledge the interrupt
    *R(VIRTIO_MMIO_INTERRUPT_ACK) = *R(VIRTIO_MMIO_INTERRUPT_STATUS) & 0x3;

    release(&net.vnet_lock);
}