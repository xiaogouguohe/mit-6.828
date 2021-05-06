// Search for and parse the multiprocessor configuration table
// See http://developer.intel.com/design/pentium/datashts/24201606.pdf

#include <inc/types.h>
#include <inc/string.h>
#include <inc/memlayout.h>
#include <inc/x86.h>
#include <inc/mmu.h>
#include <inc/env.h>
#include <kern/cpu.h>
#include <kern/pmap.h>

struct CpuInfo cpus[NCPU];
struct CpuInfo *bootcpu;
int ismp;
int ncpu;

// Per-CPU kernel stacks
/* 所有 CPU 的内核栈 */
unsigned char percpu_kstacks[NCPU][KSTKSIZE]
__attribute__ ((aligned(PGSIZE)));


// See MultiProcessor Specification Version 1.[14]

/* mp结构存储了关于CPU配置表 configuration table 的一些信息，
包括physaddr指明了 configuration table 表头的物理起始地址 */
struct mp {             // floating pointer [MP 4.1]
	/* 标志这是一个mp结构，方便查找 */
	uint8_t signature[4];           // "_MP_"
	/* mp config table 的物理地址 */
	physaddr_t physaddr;            // phys addr of MP config table
	uint8_t length;                 // 1
	uint8_t specrev;                // [14]
	uint8_t checksum;               // all bytes must add up to 0
	uint8_t type;                   // MP system config type
	uint8_t imcrp;
	uint8_t reserved[3];
} __attribute__((__packed__));

struct mpconf {         // configuration table header [MP 4.2]
	uint8_t signature[4];           // "PCMP"
	uint16_t length;                // total table length
	uint8_t version;                // [14]
	uint8_t checksum;               // all bytes must add up to 0
	uint8_t product[20];            // product id
	physaddr_t oemtable;            // OEM table pointer
	uint16_t oemlength;             // OEM table length
	/* 表项的数目 */
	uint16_t entry;                 // entry count
	/* 所有CPU的LAPIC的物理起始地址 */
	physaddr_t lapicaddr;           // address of local APIC
	uint16_t xlength;               // extended table length
	uint8_t xchecksum;              // extended table checksum
	uint8_t reserved;
	/* 表项指针，指向一个表项，也就是mpproc结构 */
	uint8_t entries[0];             // table entries
} __attribute__((__packed__));

/* CPU配置表的表项内容 */
struct mpproc {         // processor table entry [MP 4.3.1]
	uint8_t type;                   // entry type (0)
	uint8_t apicid;                 // local APIC id
	uint8_t version;                // local APIC version
	uint8_t flags;                  // CPU flags
	uint8_t signature[4];           // CPU signature
	uint32_t feature;               // feature flags from CPUID instruction
	uint8_t reserved[8];
} __attribute__((__packed__));

// mpproc flags
#define MPPROC_BOOT 0x02                // This mpproc is the bootstrap processor

// Table entry types
#define MPPROC    0x00  // One per processor
#define MPBUS     0x01  // One per bus
#define MPIOAPIC  0x02  // One per I/O APIC
#define MPIOINTR  0x03  // One per bus interrupt source
#define MPLINTR   0x04  // One per system interrupt source

/* 计算从虚拟地址addr开始，len字节长度的地址范围的每个字节的值之和 */
static uint8_t
sum(void *addr, int len)
{
	int i, sum;

	sum = 0;
	for (i = 0; i < len; i++)
		sum += ((uint8_t *)addr)[i];
	return sum;
}

// Look for an MP structure in the len bytes at physical address addr.
/* 在物理地址a开始，长度为len字节的地址范围内，找mp结构 */
static struct mp *
mpsearch1(physaddr_t a, int len)
{
	struct mp *mp = KADDR(a), *end = KADDR(a + len);

	/* 查找 "_MP_" 标识符 */
	for (; mp < end; mp++)
		if (memcmp(mp->signature, "_MP_", 4) == 0 &&
			/* mp结构体的所有字节的和为0，校验 */
		    sum(mp, sizeof(*mp)) == 0)
			return mp;
	return NULL;
}

// Search for the MP Floating Pointer Structure, which according to
// [MP 4] is in one of the following three locations:
// 1) in the first KB of the EBDA;
// 2) if there is no EBDA, in the last KB of system base memory;
// 3) in the BIOS ROM between 0xE0000 and 0xFFFFF.
/* 根据上述3种情况，在上述3个位置找mp结构，
情况 1) 在什么地址？？？BIOS的数据和BIOS ROM有什么区别？？？
情况 2) 在什么地址？？？
情况 3) 应该是[0xF0000, 0x100000) ？？？ */
static struct mp *
mpsearch(void)
{
	uint8_t *bda;
	uint32_t p;
	struct mp *mp;

	static_assert(sizeof(*mp) == 16);

	// The BIOS data area lives in 16-bit segment 0x40.
	bda = (uint8_t *) KADDR(0x40 << 4);

	// [MP 4] The 16-bit segment of the EBDA is in the two bytes
	// starting at byte 0x0E of the BDA.  0 if not present.
	if ((p = *(uint16_t *) (bda + 0x0E))) {
		p <<= 4;	// Translate from segment to PA
		if ((mp = mpsearch1(p, 1024)))
			return mp;
	} else {
		// The size of base memory, in KB is in the two bytes
		// starting at 0x13 of the BDA.
		/* 情况2)， */
		p = *(uint16_t *) (bda + 0x13) * 1024;
		if ((mp = mpsearch1(p - 1024, 1024)))
			return mp;
	}

	/* 情况3)，在BIOS ROM，也就是960KB ~ 1MB */
	return mpsearch1(0xF0000, 0x10000);
}

// Search for an MP configuration table.  For now, don't accept the
// default configurations (physaddr == 0).
// Check for the correct signature, checksum, and version.
/* 找到CPU配置表表头mpconf */ 
static struct mpconf *
mpconfig(struct mp **pmp)
{
	struct mpconf *conf;
	struct mp *mp;

	/* 没找到mp结构 */
	if ((mp = mpsearch()) == 0)
		return NULL;
	if (mp->physaddr == 0 || mp->type != 0) {
		cprintf("SMP: Default configurations not implemented\n");
		return NULL;
	}

	/* 根据 mp 结构，找到 CPU 配置表结构 mpconf */
	conf = (struct mpconf *) KADDR(mp->physaddr);
	if (memcmp(conf, "PCMP", 4) != 0) {
		cprintf("SMP: Incorrect MP configuration table signature\n");
		return NULL;
	}
	if (sum(conf, conf->length) != 0) {
		cprintf("SMP: Bad MP configuration checksum\n");
		return NULL;
	}
	if (conf->version != 1 && conf->version != 4) {
		cprintf("SMP: Unsupported MP version %d\n", conf->version);
		return NULL;
	}
	if ((sum((uint8_t *)conf + conf->length, conf->xlength) + conf->xchecksum) & 0xff) {
		cprintf("SMP: Bad MP configuration extended checksum\n");
		return NULL;
	}
	*pmp = mp;
	return conf;
}

void
mp_init(void)
{
	struct mp *mp;
	struct mpconf *conf;
	struct mpproc *proc;
	uint8_t *p;
	unsigned int i;

	/* bootcpu 选第一块cpu */
	bootcpu = &cpus[0];
	/* 没找到CPU配置表 mpconf 结构 */
	if ((conf = mpconfig(&mp)) == 0)
		return;
	ismp = 1;
	lapicaddr = conf->lapicaddr;

	/* 遍历CPU配置表 */
	for (p = conf->entries, i = 0; i < conf->entry; i++) {
		/* p 指向 mpproc 结构的第一个成员，即type 成员，
		根据 mpproc 的类型，判断它表示什么类型的CPU */
		switch (*p) {
		case MPPROC:
			cprintf("in func mp_init, case is MPPROC\n");
			proc = (struct mpproc *)p;
			/* 判断当前 CPU 是否为 boot CPU */
			/* ncpu 初始化为0 */
			if (proc->flags & MPPROC_BOOT) {
				cprintf("in func mp_init, is boot, ncpu: %d\n", ncpu);
				bootcpu = &cpus[ncpu];
			}
			/* 把当前 CPU 记录到 cpus 数组 */
			if (ncpu < NCPU) {
				cprintf("in func mp_init, ncpu: %d\n", ncpu);
				cpus[ncpu].cpu_id = ncpu;
				ncpu++;
			} else {
				cprintf("SMP: too many CPUs, CPU %d disabled\n",
					proc->apicid);
			}
			p += sizeof(struct mpproc);
			continue;
		case MPBUS:
		case MPIOAPIC:
		case MPIOINTR:
		case MPLINTR:
			p += 8;
			continue;
		default:
			cprintf("mpinit: unknown config type %x\n", *p);
			ismp = 0;
			i = conf->entry;
		}
	}

	bootcpu->cpu_status = CPU_STARTED;

	/* 没有 MP 结构，或发现了无法识别类型的 mpproc 结构，回退到没有 MP 的情况 */
	if (!ismp) {
		// Didn't like what we found; fall back to no MP.
		ncpu = 1;
		lapicaddr = 0;
		cprintf("SMP: configuration not found, SMP disabled\n");
		return;
	}
	cprintf("SMP: CPU %d found %d CPU(s)\n", bootcpu->cpu_id,  ncpu);

	if (mp->imcrp) {
		// [MP 3.2.6.1] If the hardware implements PIC mode,
		// switch to getting interrupts from the LAPIC.
		cprintf("SMP: Setting IMCR to switch from PIC mode to symmetric I/O mode\n");
		outb(0x22, 0x70);   // Select IMCR
		outb(0x23, inb(0x23) | 1);  // Mask external interrupts.
	}
}
