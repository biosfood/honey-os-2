section .sharedFunctions
bits 32

global syscallStub
global handleSyscallEnd
extern handleSyscall
extern temporaryESP

temporaryEAX: resb 4

syscallStub:
  mov [temporaryEAX], eax
  mov eax, 0x500000
  mov cr3, eax
  mov eax, [temporaryEAX]
  push esi
  push edx
  push ecx
  push ebx
  push eax
  push edi
  call handleSyscall
handleSyscallEnd:
  mov eax, [temporaryESP]
  mov esp, eax
  pop ebp
; going to the end of runFunction() now
  ret

global runFunction
global runEnd
global thread_cr3
global thread_esp
global thread_return_value

temporaryESP: resb 4
thread_cr3: resb 4
thread_esp: resb 4
thread_return_value: resb 4

runFunction:
  push ebp
  mov eax, esp
  mov [temporaryESP], eax
  mov ecx, [thread_esp]
  mov edx, returnPoint
  mov eax, [thread_cr3]
  mov ebx, [thread_return_value]
  mov cr3, eax
  mov eax, ebx
  xor ebp, ebp
  sti
  sysexit
runEnd:
  mov ebx, eax
  mov eax, 0
  sysenter

returnPoint:
  ret
