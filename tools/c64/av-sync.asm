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

        lda #$00
        sta $d020               ; border black
        sta $d021               ; background black
        sta $d418               ; SID volume off
        sta $d404               ; gate off (voice 1)

        jsr detect_pal_ntsc     ; set C3 frequency based on PAL/NTSC

        lda #$00
        sta $d405               ; ADSR: attack=0, decay=0
        lda #$f0
        sta $d406               ; ADSR: sustain=max, release=0

        lda #$00
        sta $d402               ; pulse width low
        lda #$08
        sta $d403               ; pulse width high (50%)

main_loop:
        jsr wait_space
        jsr key_pressed_loop
        jmp main_loop

;--------------------------
; Wait until SPACE is pressed
;--------------------------
wait_space:
        lda $dc01               ; read CIA#1 keyboard port A
        and #%00010000          ; mask column for SPACE
        cmp #%00000000          ; active low (0 = pressed)
        bne wait_space
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

        lda #%01000001          ; pulse waveform + gate on
        sta $d404               ; control register for voice 1

check_key:
        lda $dc01
        and #%00010000
        cmp #%00000000
        beq check_key            ; loop while space held

        ; key released: stop sound and restore colors
        lda #$00
        sta $d404                ; turn off SID gate
        sta $d418                ; SID volume off
        lda #$00
        sta $d020                ; reset border to black
        sta $d021                ; reset background to black
        rts

;--------------------------
; Detect PAL vs NTSC and set C3 frequency
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

        ; NTSC (~124.9 Hz for word $0008)
        lda #$08
        sta $d400                ; frequency low byte
        lda #$00
        sta $d401                ; frequency high byte
        rts

set_pal:
        lda #$09
        sta $d400                ; frequency low byte
        lda #$00
        sta $d401                ; frequency high byte
        rts
