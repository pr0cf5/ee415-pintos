#include "threads/interrupt.h"

void vm_init();
void vm_handle_user_fault(void *uaddr, struct intr_frame *f);