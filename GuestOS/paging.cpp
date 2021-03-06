#include <stdio.h>
#include <string.h>
#include "paging.h"
#include "os.h"

PAGER::PAGER()
{
	m_page_dir = 0;
	m_page_table_base= PAGE_TABLE_PHYSICAL_ADDR;

	m_page_frame_min=0;
	m_page_frame_max=0;
	m_next_free_page_frame=0;
	m_page_frame_database=NULL;
	m_database_usable=false;
}


PAGER::~PAGER()
{
}

bool  PAGER::Init(uint32_t page_frame_min, uint32_t page_frame_max)
{
	m_page_table_base = PAGE_TABLE_PHYSICAL_ADDR;

	m_page_frame_min = page_frame_min;
	m_page_frame_max = page_frame_max;
	m_next_free_page_frame = m_page_frame_min;
	m_database_usable = false;

	__asm mov eax, page_frame_min
	__asm mov ebx, PAGE_TABLE_PHYSICAL_ADDR
	__asm mov edx, PAGE_TABLE_PHYSICAL_ADDR
	//__asm hlt

	m_page_dir = new_page_dir();
	//__asm hlt
	identity_paging_lowest_4M();
	startup_page_mode();

	return true;
}

void   PAGER::startup_page_mode()
{
	uint32_t page_dir = (uint32_t)m_page_dir;

	__asm mov		eax, dword ptr[page_dir]
	__asm mov		cr3, eax
	__asm mov		eax, cr0
	__asm or		eax, 0x80000000
	__asm mov		cr0, eax
}

//该函数必须在开启分页之前调用
uint32_t PAGER::new_page_dir()
{
	//分配页目录物理页面
	m_page_dir = alloc_physical_page();
	memset((void*)m_page_dir, 0, PAGE_SIZE);
	//自映射
	((uint32_t*)m_page_dir)[PAGE_TABLE_PHYSICAL_ADDR >> 22] = m_page_dir | PT_PRESENT | PT_WRITABLE;
	return m_page_dir;
}

uint32_t PAGER::new_page_table(uint32_t virtual_address)
{
	CHECK_PAGE_ALGINED(virtual_address);

	uint32_t  page_table_PA = alloc_physical_page();
	uint32_t  val = page_table_PA | PT_PRESENT | PT_WRITABLE;
	if (USER_SPACE(virtual_address)) val |= PT_USER;
	SET_PDE(virtual_address, val);
	uint32_t page_table_VA = GET_PAGE_TABLE(virtual_address);
	memset((void*)page_table_VA, 0, PAGE_SIZE);
	return page_table_VA;
}

//该函数必须在开启分页之前调用
//http://wiki.osdev.org/Identity_Paging
bool     PAGER::identity_paging_lowest_4M()
{
	uint32_t  page_table_0 = alloc_physical_page();
	((uint32_t*)m_page_dir)[0] = page_table_0 | PT_PRESENT | PT_WRITABLE;
	for (int i = 0; i < 1024;i++)
	{
		((uint32_t*)page_table_0)[i] = i | PT_PRESENT | PT_WRITABLE | PT_USER;
	}
	return true;
}

void PAGER::map_pages(uint32_t physical_addr, uint32_t virtual_addr, uint32_t size, uint32_t protect)
{
	//if (USER_SPACE(virtual_addr)) 
	protect |= PT_USER;
	int pages = SIZE_TO_PAGES(size);
	for (int i = 0; i < pages; i++)
	{
		uint32_t pde_index = PDE_INDEX(virtual_addr);
		uint32_t pte_index = PTE_INDEX(virtual_addr);
		uint32_t* page_table = (uint32_t*)(PAGE_TABLE_VIRTUAL_ADDR + pde_index*PAGE_SIZE);
		page_table[pte_index] = physical_addr | protect;
		__asm  invlpg virtual_addr
		physical_addr += PAGE_SIZE;
		virtual_addr += PAGE_SIZE;
	}
}

void PAGER::unmap_pages(uint32_t virtual_addr, uint32_t size)
{
	int pages = SIZE_TO_PAGES(size);
	for (uint32_t i = 0; i < pages; i++)
	{
		uint32_t pde_index = PDE_INDEX(virtual_addr);
		uint32_t pte_index = PTE_INDEX(virtual_addr);
		uint32_t* page_table = (uint32_t*)(PAGE_TABLE_VIRTUAL_ADDR + pde_index*PAGE_SIZE);
		page_table[pte_index] = 0;
		__asm  invlpg virtual_addr
		virtual_addr += PAGE_SIZE;
	}
}

bool     PAGER::create_database()
{
	//为page_frame_db分配1M物理内存,映射到0xC00400000-0xC004FFFFF
	uint32_t  database_PA = alloc_physical_pages(SIZE_TO_PAGES(MB(1)));
	map_pages(database_PA, PAGE_FRAME_BASE, MB(1));
	memset((void*)PAGE_FRAME_BASE, PAGE_FREE, MB(1));

	m_next_free_page_frame = m_page_frame_min;
	m_database_usable = true;
	return true;
}

uint32_t PAGER::alloc_physical_page()
{
	if (!m_database_usable)
	{
		uint32_t addr = m_page_frame_min * PAGE_SIZE;
		m_page_frame_min++;
		m_next_free_page_frame = m_page_frame_min;
		return addr;
	}

	for (uint32_t i = m_next_free_page_frame; i < m_page_frame_max; i++)
	{
		if (m_page_frame_database[i] == PAGE_FREE)
		{
			m_next_free_page_frame = i + 1;
			return i*PAGE_SIZE;
		}
	}
	return 0;
}

void   PAGER::free_physical_page(uint32_t page)
{
	m_page_frame_database[page] = PAGE_FREE;
	if (page < m_next_free_page_frame)
	{
		m_next_free_page_frame = page;
	}
}

uint32_t PAGER::alloc_physical_pages(uint32_t pages)
{
	if (!m_database_usable)
	{
		uint32_t addr = m_page_frame_min * PAGE_SIZE;
		m_page_frame_min += pages;
		m_next_free_page_frame = m_page_frame_min;
		return addr;
	}

	for (uint32_t i = m_next_free_page_frame; i < m_page_frame_max - pages; i++)
	{
		if (m_page_frame_database[i] == PAGE_FREE)
		{
			uint32_t j = i;
			for (j++; j < i + pages && j < m_page_frame_max; j++)
			{
				if (m_page_frame_database[j] != PAGE_FREE) break;
			}
			if (j = i + pages)
			{
				m_next_free_page_frame = j;
				return i*PAGE_SIZE;
			}
		}
	}
	return 0;
}

void   PAGER::free_physical_pages(uint32_t  start_page, uint32_t pages)
{
	for (uint32_t i = start_page; i < start_page + pages; i++)
	{
		m_page_frame_database[i] = PAGE_FREE;
	}
	if (m_next_free_page_frame > start_page)
	{
		m_next_free_page_frame = start_page;
	}
}
