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

        lda #$00
        sta $d020               ; border black
        sta $d021               ; background black
        sta $d418               ; SID volume off
        sta $d404               ; gate off (voice 1)

        jsr detect_video_standard

        lda #$00
        sta $d405               ; ADSR: attack=0, decay=0
        lda #$f0
        sta $d406               ; ADSR: sustain=max, release=0

        lda #$00
        sta $d402               ; pulse width low
        lda #$08
        sta $d403               ; pulse width high (50%)

        lda #<irq_handler
        sta $0314
        lda #>irq_handler
        sta $0315

        lda #$7f
        sta $dc0d               ; disable CIA interrupts
        sta $dd0d
        lda $dc0d               ; clear pending CIA interrupts
        lda $dd0d

        lda #$01
        sta $d01a               ; enable raster IRQ
        lda start_line_low
        sta $d012
        lda $d011
        and #%01111111
        ora start_line_high
        sta $d011
        lda #$01
        sta $d019               ; acknowledge any pending IRQs

        cli                     ; enable interrupts

main_loop:
        jmp main_loop

;--------------------------
; Raster IRQ handler
;--------------------------
irq_handler:
        pha
        txa
        pha
        tya
        pha

        lda pop_counter
        bne dec_counter

        jsr av_pop_one_frame
        lda #47
        sta pop_counter
        jmp irq_done

dec_counter:
        dec pop_counter

irq_done:
        lda #$01
        sta $d019               ; acknowledge raster IRQ

        pla
        tay
        pla
        tax
        pla
        rti

;--------------------------
; One-frame A/V pop
;--------------------------
av_pop_one_frame:
        lda #$01
        sta $d020               ; border white
        sta $d021               ; background white

        lda #$0f
        sta $d418               ; SID volume max
        lda #%01000001
        sta $d404               ; pulse waveform + gate on

        jsr wait_next_frame_line2

        lda #$00
        sta $d404               ; gate off
        sta $d418               ; volume off
        sta $d020               ; border black
        sta $d021               ; background black
        rts

;--------------------------
; Wait until next frame line 2
;--------------------------
wait_next_frame_line2:
wait_wrap:
        lda $d011
        bmi wait_wrap           ; wait for raster < 256 (new frame)
wait_line0:
        lda $d012
        bne wait_line0
wait_line2:
        lda $d012
        cmp #$02
        bne wait_line2
        rts

;--------------------------
; Detect PAL vs NTSC
; - Sets C3 frequency
; - Sets raster START_LINE = MAX_RASTER_LINE - 2
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

        ; NTSC: MAX_RASTER_LINE=262, START_LINE=260
        lda #$08
        sta $d400                ; frequency low byte
        lda #$00
        sta $d401                ; frequency high byte
        lda #$04
        sta start_line_low
        lda #$80
        sta start_line_high
        rts

set_pal:
        ; PAL: MAX_RASTER_LINE=311, START_LINE=309
        lda #$09
        sta $d400                ; frequency low byte
        lda #$00
        sta $d401                ; frequency high byte
        lda #$35
        sta start_line_low
        lda #$80
        sta start_line_high
        rts

; Variables
start_line_low: .byte $00
start_line_high: .byte $00
pop_counter: .byte $00
