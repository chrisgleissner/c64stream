; C64 Audio/Video Sync Test (Automatic)
; Generates a one-frame A/V pop every 48 frames at a precise raster line

*=$0801
basic_stub:
        .word basic_end         ; pointer to next line
        .word 10                ; line number 10
        .byte $9e               ; SYS token
        .byte " "
        .text format("%4d", start)  ; SYS address auto-calculated
        .byte 0                 ; end of BASIC line
basic_end:
        .word 0                 ; end of BASIC program

start:
        sei                     ; disable interrupts

        ; Ensure I/O is mapped in ($D000-$DFFF)
        lda #$37
        sta $01

        ; Clear screen to spaces first
        jsr clear_screen

        ; Make entire screen white (color RAM). This makes the pop-frame
        ; background/border flip yield a true full-white flash.
        jsr clear_color_ram

        lda #$00
        sta $d020               ; border black
        sta $d021               ; background black

        ; Disable all sprites to ensure the pop frame is truly solid white.
        sta $d015               ; sprite enable bits = 0

        ; Initialize SID - clear all registers first
        ldx #$18
clear_sid:
        sta $d400,x
        dex
        bpl clear_sid

        ; Set up ADSR for voice 1: instant attack, high sustain
        lda #$00
        sta $d405               ; ADSR: attack=0, decay=0
        lda #$f0
        sta $d406               ; ADSR: sustain=max, release=0

        ; Initialize pop counter
        lda #48
        sta pop_counter

        lda #$00
        sta flash_active

        ; Detect PAL/NTSC to compute the "end-of-frame" raster line.
        ; We start the flash there so the *next* full frame is guaranteed white.
        jsr detect_video_standard

        ; Set up IRQ vector
        lda #<irq_handler
        sta $0314
        lda #>irq_handler
        sta $0315

        lda #$7f
        sta $dc0d               ; disable CIA1 interrupts
        sta $dd0d               ; disable CIA2 interrupts
        lda $dc0d               ; clear pending CIA1 interrupts
        lda $dd0d               ; clear pending CIA2 interrupts

        lda #$01
        sta $d01a               ; enable raster IRQ

        ; Start with IRQ at end-of-frame (PAL/NTSC-specific).
        jsr schedule_irq_end_line

        lda #$01
        sta $d019               ; acknowledge any pending VIC IRQs

        cli                     ; enable interrupts

main_loop:
        jmp main_loop

;--------------------------
; Clear screen memory to spaces ($20)
;--------------------------
clear_screen:
        lda #$20                ; space character
        ldx #$00
clear_loop_first_232:
        sta $0400,x
        sta $0500,x
        sta $0600,x
        sta $0700,x
        inx
        cpx #$e8                ; 232 bytes
        bne clear_loop_first_232

clear_loop_last_24:
        sta $0400,x
        sta $0500,x
        sta $0600,x
        inx
        bne clear_loop_last_24  ; until X wraps to 0
        rts

;--------------------------
; Set full screen color RAM to white ($01)
;--------------------------
clear_color_ram:
        lda #$01
        ldx #$00
ccr_loop_first_232:
        sta $d800,x
        sta $d900,x
        sta $da00,x
        sta $db00,x
        inx
        cpx #$e8
        bne ccr_loop_first_232

ccr_loop_last_24:
        sta $d800,x
        sta $d900,x
        sta $da00,x
        inx
        bne ccr_loop_last_24
        rts

;--------------------------
; Raster IRQ handler
;--------------------------
irq_handler:
        ; NOTE: We hook the KERNAL IRQ vector ($0314/$0315). The ROM IRQ code
        ; already saved registers and expects us to JMP to $EA81 to restore+RTI.
        ; Do NOT use RTI here.

        ; IRQ state machine alternates between two compare lines:
        ; - end-of-frame (max_raster-2): start flash (affects last few lines)
        ; - line 0: used only to stop flash on the first row of the frame *after*
        ;   the white frame
        ;
        ; flash_active meaning:
        ; 0 = idle (black)
        ; 1 = flash started at end-of-frame; next line0 marks entry to white frame
        ; 2 = we are in the guaranteed full white frame; next line0 stops it

        lda irq_phase
        beq irq_at_end_line

        ; ----- IRQ at line 0 -----
        lda flash_active
        beq irq_line0_done

        cmp #$01
        beq irq_mark_white_frame

        ; flash_active == 2 -> stop on first row of next frame
        jsr av_pop_stop
        lda #$00
        sta flash_active
        jmp irq_line0_done

irq_mark_white_frame:
        lda #$02
        sta flash_active

irq_line0_done:
        lda #$00
        sta irq_phase
        jsr schedule_irq_end_line
        jmp irq_done

        ; ----- IRQ at end-of-frame (PAL/NTSC-specific) -----
irq_at_end_line:
        lda flash_active
        cmp #$02
        beq irq_endline_keep_white

        ; Normal cadence counting happens at end-of-frame.
        dec pop_counter
        bne irq_endline_done

        lda #48
        sta pop_counter
        jsr av_pop_start
        lda #$01
        sta flash_active

        ; We just turned the screen white near the bottom of the frame.
        ; Schedule an IRQ at line 0 of the *next* frame so we can:
        ; - mark entry into the guaranteed full-white frame
        ; - later schedule another line-0 IRQ to stop and go back to black
        lda #$01
        sta irq_phase
        jsr schedule_irq_line0
        jmp irq_done

irq_endline_done:
        ; Stay on end-of-frame IRQ when idle.
        jmp irq_done

irq_endline_keep_white:
        ; We are in the full white frame; schedule line 0 of the next frame so we
        ; can stop on the first raster row.
        lda #$01
        sta irq_phase
        jsr schedule_irq_line0

irq_done:
        lda #$01
        sta $d019               ; acknowledge raster IRQ
        jmp $ea81

;--------------------------
; One-frame A/V pop
;--------------------------
av_pop_start:
        ; Ensure I/O is visible while touching VIC/SID registers
        lda #$37
        sta $01

        lda #$01
        sta $d020               ; border white
        sta $d021               ; background white

        lda #$0f
        sta $d418               ; SID volume max

        ; Set an audible frequency each pop
        lda #$28
        sta $d400               ; voice 1 freq lo
        lda #$00
        sta $d401               ; voice 1 freq hi
        lda #%00010001          ; triangle waveform + gate on
        sta $d404
        rts

av_pop_stop:
        ; Ensure I/O is visible while touching VIC/SID registers
        lda #$37
        sta $01

        lda #%00010000          ; triangle waveform, gate off
        sta $d404               ; release note
        lda #$00
        sta $d418               ; volume off
        sta $d020               ; border black
        sta $d021               ; background black
        rts

;--------------------------
; IRQ scheduling helpers
;--------------------------
schedule_irq_line0:
        lda #$00
        sta $d012
        lda $d011
        and #%01111111          ; bit 7 = 0 (raster < 256)
        sta $d011
        rts

schedule_irq_end_line:
        lda end_line_low
        sta $d012
        lda $d011
        and #%01111111
        ora end_line_high
        sta $d011
        rts

;--------------------------
; Detect PAL vs NTSC and compute end-of-frame line (max_raster-2)
; - NTSC: max=262 -> end_line=260 (0x104)
; - PAL:  max=311 -> end_line=309 (0x135)
;--------------------------
detect_video_standard:
        lda #$00
wait_raster0:
        cmp $d012
        bne wait_raster0
wait_raster1:
        lda $d012
        beq wait_raster1

wait_high_bit:
        lda $d011
        bpl wait_high_bit        ; wait for raster >= 256

check_pal:
        lda $d012
        cmp #$20
        bcs set_pal
        lda $d011
        bmi check_pal            ; still in high raster range

        ; NTSC: end_line = 260 (0x104)
        lda #$04
        sta end_line_low
        lda #$80
        sta end_line_high
        rts

set_pal:
        ; PAL: end_line = 309 (0x135)
        lda #$35
        sta end_line_low
        lda #$80
        sta end_line_high
        rts

; Variables
pop_counter: .byte 48
flash_active: .byte 0
irq_phase: .byte 0
end_line_low: .byte $00
end_line_high: .byte $00
