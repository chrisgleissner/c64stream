; C64 Audio/Video Sync Test
; Press SPACE to flash border/background
; and play a short tone at the precise time of the flash

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

        ; Ensure CIA1 keyboard ports are configured
        lda #$ff
        sta $dc02               ; CIA1 DDRA = output
        lda #$00
        sta $dc03               ; CIA1 DDRB = input

        ; Clear screen to spaces
        jsr clear_screen

        lda #$00
        sta $d020               ; border black
        sta $d021               ; background black

        ; Initialize SID - clear all registers first
        ldx #$18
clear_sid:
        sta $d400,x
        dex
        bpl clear_sid

        jsr detect_pal_ntsc     ; set frequency based on PAL/NTSC

        ; Set up ADSR for voice 1: instant attack, high sustain
        lda #$00
        sta $d405               ; ADSR: attack=0, decay=0
        lda #$f0
        sta $d406               ; ADSR: sustain=max, release=0

main_loop:
        jsr wait_space
        jsr key_pressed_loop
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
; Wait until SPACE is pressed
;--------------------------
wait_space:
        lda #%01111111          ; select row 7 (SPACE row)
        sta $dc00               ; write to CIA#1 port A
        lda $dc01               ; read CIA#1 port B
        and #%00010000          ; mask column 4 for SPACE
        bne wait_space          ; loop until bit is 0 (key pressed)
        rts

;--------------------------
; While key pressed: flash border/bg and play tone
;--------------------------
key_pressed_loop:
        lda #$01
        sta $d020               ; border white
        sta $d021               ; background white

        lda #$0f
        sta $d418               ; SID volume max

        lda #%00010001          ; triangle waveform + gate on
        sta $d404               ; control register for voice 1

check_key:
        lda #%01111111          ; select row 7 (SPACE row)
        sta $dc00
        lda $dc01
        and #%00010000
        beq check_key            ; loop while space held (bit=0)

        ; key released: stop sound and restore colors
        lda #%00010000          ; triangle waveform, gate off
        sta $d404               ; release note properly
        lda #$00
        sta $d418               ; SID volume off
        sta $d020               ; reset border to black
        sta $d021               ; reset background to black
        rts

;--------------------------
; Detect PAL vs NTSC and set frequency
;--------------------------
detect_pal_ntsc:
        lda #$00
wait_line0:
        cmp $d012
        bne wait_line0
wait_line1:
        lda $d012
        beq wait_line1

wait_high_bit:
        lda $d011
        bpl wait_high_bit        ; wait for raster >= 256

check_pal:
        lda $d012
        cmp #$20
        bcs set_pal
        lda $d011
        bmi check_pal            ; still in high raster range

        ; NTSC frequency (~262 Hz for audible tone)
        lda #$d2
        sta $d400                ; frequency low byte
        lda #$10
        sta $d401                ; frequency high byte
        rts

set_pal:
        ; PAL frequency (~262 Hz for audible tone)
        lda #$10
        sta $d400                ; frequency low byte
        lda #$11
        sta $d401                ; frequency high byte
        rts
