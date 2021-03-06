; RUN: llc < %s -mtriple=x86_64-unknown-linux-gnu -disable-cfi | FileCheck --check-prefix=STATIC %s
; RUN: llc < %s -mtriple=x86_64-unknown-linux-gnu -disable-cfi -relocation-model=pic | FileCheck --check-prefix=PIC %s

; STATIC:      .ascii   "zPLR"
; STATIC:      .byte   3
; STATIC-NEXT: .long   __gxx_personality_v0
; STATIC-NEXT: .byte   3
; STATIC-NEXT: .byte   3

; PIC:      .ascii   "zPLR"
; PIC:      .byte   155
; PIC-NEXT: .L
; PIC-NEXT: .long   DW.ref.__gxx_personality_v0-.L
; PIC-NEXT: .byte   27
; PIC-NEXT: .byte   27


define void @bar() {
entry:
  %call = invoke i32 @foo()
          to label %invoke.cont unwind label %lpad

invoke.cont:
  ret void

lpad:
  %exn = call i8* @llvm.eh.exception() nounwind
  %eh.selector = call i32 (i8*, i8*, ...)* @llvm.eh.selector(i8* %exn, i8* bitcast (i32 (...)* @__gxx_personality_v0 to i8*), i8* null) nounwind
  ret void
}

declare i32 @foo()

declare i8* @llvm.eh.exception() nounwind readonly

declare i32 @__gxx_personality_v0(...)

declare i32 @llvm.eh.selector(i8*, i8*, ...) nounwind
