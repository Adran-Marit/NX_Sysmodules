/*
 * Copyright (c) 2018 Atmosphère-NX, Reisyukaku
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
 
#include <switch.h>

#include "fatal_task_screen.hpp"
#include "fatal_config.hpp"
#include "fatal_font.hpp"
#include "logo.hpp"

static constexpr u32 FatalScreenWidth = 1280;
static constexpr u32 FatalScreenHeight = 720;
static constexpr u32 FatalScreenBpp = 2;

static constexpr u32 FatalScreenWidthAlignedBytes = (FatalScreenWidth * FatalScreenBpp + 63) & ~63;
static constexpr u32 FatalScreenWidthAligned = FatalScreenWidthAlignedBytes / FatalScreenBpp;

u32 GetPixelOffset(uint32_t x, uint32_t y)
{
    u32 tmp_pos;

    tmp_pos = ((y & 127) / 16) + (x/32*8) + ((y/16/8)*(((FatalScreenWidthAligned/2)/16*8)));
    tmp_pos *= 16*16 * 4;

    tmp_pos += ((y%16)/8)*512 + ((x%32)/16)*256 + ((y%8)/2)*64 + ((x%16)/8)*32 + (y%2)*16 + (x%8)*2;//This line is a modified version of code from the Tegra X1 datasheet.

    return tmp_pos / 2;
}

Result ShowFatalTask::SetupDisplayInternal() {
    Result rc;
    ViDisplay display;
    /* Try to open the display. */
    if (R_FAILED((rc = viOpenDisplay("Internal", &display)))) {
        if (rc == 0xE72) {
            return 0;
        } else {
            return rc;
        }
    }
    /* Guarantee we close the display. */
    ON_SCOPE_EXIT { viCloseDisplay(&display); };
    
    /* Turn on the screen. */
    if (R_FAILED((rc = viSetDisplayPowerState(&display, ViPowerState_On)))) {
        return rc;
    }
    
    /* Set alpha to 1.0f. */
    if (R_FAILED((rc = viSetDisplayAlpha(&display, 1.0f)))) {
        return rc;
    }
    
    return rc;
}

Result ShowFatalTask::SetupDisplayExternal() {
    Result rc;
    ViDisplay display;
    /* Try to open the display. */
    if (R_FAILED((rc = viOpenDisplay("External", &display)))) {
        if (rc == 0xE72) {
            return 0;
        } else {
            return rc;
        }
    }
    /* Guarantee we close the display. */
    ON_SCOPE_EXIT { viCloseDisplay(&display); };
    
    /* Set alpha to 1.0f. */
    if (R_FAILED((rc = viSetDisplayAlpha(&display, 1.0f)))) {
        return rc;
    }
    
    return rc;
}

Result ShowFatalTask::PrepareScreenForDrawing() {
    Result rc = 0;
    
    /* Connect to vi. */
    if (R_FAILED((rc = viInitialize(ViServiceType_Manager)))) {
        return rc;
    }
    
    /* Close other content. */
    viSetContentVisibility(false);
    
    /* Setup the two displays. */
    if (R_FAILED((rc = SetupDisplayInternal())) || R_FAILED((rc = SetupDisplayExternal()))) {
        return rc;
    }
    
    /* Open the default display. */
    if (R_FAILED((rc = viOpenDefaultDisplay(&this->display)))) {
        return rc;
    }
    
    /* Reset the display magnification to its default value. */
    u32 display_width, display_height;
    if (R_FAILED((rc = viGetDisplayLogicalResolution(&this->display, &display_width, &display_height)))) {
        return rc;
    }
    if (R_FAILED((rc = viSetDisplayMagnification(&this->display, 0, 0, display_width, display_height)))) {
        return rc;
    }
    
    /* Create layer to draw to. */
    if (R_FAILED((rc = viCreateLayer(&this->display, &this->layer)))) {
        return rc;
    }
    
    /* Setup the layer. */
    {
        /* Display a layer of 1280 x 720 at 1.5x magnification */
        /* NOTE: N uses 2 (770x400) RGBA4444 buffers (tiled buffer + linear). */
        /* We use a single 1280x720 tiled RGB565 buffer. */
        constexpr u32 raw_width = FatalScreenWidth;
        constexpr u32 raw_height = FatalScreenHeight;
        constexpr u32 layer_width = ((raw_width) * 3) / 2;
        constexpr u32 layer_height = ((raw_height) * 3) / 2;
        
        const float layer_x = static_cast<float>((display_width - layer_width) / 2);
        const float layer_y = static_cast<float>((display_height - layer_height) / 2);
        u64 layer_z;
        
        if (R_FAILED((rc = viSetLayerSize(&this->layer, layer_width, layer_height)))) {
            return rc;
        }
        
        /* Set the layer's Z at display maximum, to be above everything else .*/
        /* NOTE: Fatal hardcodes 100 here. */
        if (R_SUCCEEDED((rc = viGetDisplayMaximumZ(&this->display, &layer_z)))) {
            if (R_FAILED((rc = viSetLayerZ(&this->layer, layer_z)))) {
                return rc;
            }
        }
        
        /* Center the layer in the screen. */
        if (R_FAILED((rc = viSetLayerPosition(&this->layer, layer_x, layer_y)))) {
            return rc;
        }
    
        /* Create framebuffer. */
        if (R_FAILED(rc = nwindowCreateFromLayer(&this->win, &this->layer))) {
            return rc;
        }
        if (R_FAILED(rc = framebufferCreate(&this->fb, &this->win, raw_width, raw_height, PIXEL_FORMAT_RGB_565, 1))) {
            return rc;
        }
    }
    

    return rc;
}

Result ShowFatalTask::ShowFatal() {
    Result rc = 0;
    const FatalConfig *config = GetFatalConfig();

    if (R_FAILED((rc = PrepareScreenForDrawing()))) {
        *(volatile u32 *)(0xCAFEBABE) = rc;
        return rc;
    }
    
    /* Dequeue a buffer. */
    u16 *tiled_buf = reinterpret_cast<u16 *>(framebufferBegin(&this->fb, NULL));
    if (tiled_buf == nullptr) {
        return FatalResult_NullGfxBuffer;
    }
    
    /* Let the font manager know about our framebuffer. */
    FontManager::ConfigureFontFramebuffer(tiled_buf, GetPixelOffset);
    FontManager::SetFontColor(0xFFFF);
    
    /* Draw a background. */
    for (size_t i = 0; i < this->fb.fb_size / sizeof(*tiled_buf); i++) {
        tiled_buf[i] = LogoBin[0];
    }
    
    /* Draw the atmosphere logo in the bottom right corner. */
    for (size_t y = 0; y < HEIGHT; y++) {
        for (size_t x = 0; x < WIDTH; x++) {
            tiled_buf[GetPixelOffset(32 + x, FatalScreenHeight - HEIGHT + y)] = LogoBin[y * WIDTH + x];
        }
    }
    
    // Human readable info
    FontManager::SetPosition(32, 64);
    FontManager::SetFontSize(16.0f);
    FontManager::PrintFormat(config->error_msg, R_MODULE(this->ctx->error_code), R_DESCRIPTION(this->ctx->error_code), this->ctx->error_code);
    FontManager::AddSpacingLines(0.5f);
    FontManager::PrintFormatLine("Meaning: %s", getMeaning(this->ctx->error_code).c_str());
    FontManager::AddSpacingLines(0.5f);
    FontManager::PrintFormatLine("Title: %016lX", this->title_id);
    FontManager::AddSpacingLines(0.5f);
    FontManager::PrintFormatLine(u8"Firmware: %s ReiNX", GetFatalConfig()->firmware_version.display_version);
    FontManager::AddSpacingLines(1.5f);
    FontManager::Print(config->error_desc);
    FontManager::AddSpacingLines(1.5f);
    FontManager::PrintLine("Troubleshooting:");
    FontManager::AddSpacingLines(0.5f);
    
    u16 linkCol = 0x641F;
    FontManager::Print("Guide: ");
    FontManager::SetFontColor(linkCol);
    FontManager::PrintLine("https://reinx.guide/");
    FontManager::SetFontColor(0xFFFF);
    FontManager::AddSpacingLines(0.5f);
    FontManager::Print("Discord: ");
    FontManager::SetFontColor(linkCol);
    FontManager::PrintLine("https://discord.gg/NxpeNwz");
    FontManager::SetFontColor(0xFFFF);
    
    /* Add a line. */
    FontManager::SetPosition(660, 0);
    for (size_t y = 32; y < FatalScreenHeight - 32; y++) {
        tiled_buf[GetPixelOffset(FontManager::GetX(), y)] = 0xFFFF;
    }
    
    //Print debug info
    FontManager::SetFontSize(14.0f);
    
    if (this->ctx->cpu_ctx.is_aarch32) {
        //Registers
        FontManager::SetPosition(675, 64);
        FontManager::PrintLine("Arm32 Registers:");
        for (size_t i = 0; i < NumAarch32Gprs; i++) {
            u32 x = FontManager::GetX();
            FontManager::PrintFormat("%s:", Aarch32GprNames[i]);
            FontManager::SetPosition(x + 48, FontManager::GetY());
            FontManager::PrintFormatLine("%08lX", this->ctx->has_gprs[i] ? this->ctx->cpu_ctx.aarch32_ctx.r[i] : 0);
            FontManager::SetPosition(x, FontManager::GetY());
        }
        FontManager::Print("PC:   ");
        FontManager::PrintFormatLine("%08lX", this->ctx->cpu_ctx.aarch32_ctx.pc);
        //Backtrace
        FontManager::SetPosition(950, 64);
        FontManager::PrintLine("Start Address: ");
        FontManager::PrintFormatLine("%08lX", this->ctx->cpu_ctx.aarch32_ctx.start_address);
        FontManager::AddSpacingLines(0.5f);
        FontManager::PrintLine("Backtrace: ");
        for (u32 i = 0; i < Aarch32CpuContext::MaxStackTraceDepth / 2; i++) {
            u32 bt_cur = 0, bt_next = 0;
            if (i < this->ctx->cpu_ctx.aarch32_ctx.stack_trace_size) {
                bt_cur = this->ctx->cpu_ctx.aarch32_ctx.stack_trace[i];
            }
            if (i + Aarch32CpuContext::MaxStackTraceDepth / 2 < this->ctx->cpu_ctx.aarch32_ctx.stack_trace_size) {
                bt_next = this->ctx->cpu_ctx.aarch32_ctx.stack_trace[i + Aarch32CpuContext::MaxStackTraceDepth / 2];
            }
            
            if (i < this->ctx->cpu_ctx.aarch32_ctx.stack_trace_size) {
                u32 x = FontManager::GetX();
                FontManager::PrintFormat("BT[%02d]: ", i);
                FontManager::SetPosition(x + 72, FontManager::GetY());
                FontManager::PrintFormatLine("%08lX", bt_cur);
            }
            
            if (i + Aarch32CpuContext::MaxStackTraceDepth / 2 < this->ctx->cpu_ctx.aarch32_ctx.stack_trace_size) {
                u32 x = FontManager::GetX();
                FontManager::PrintFormat("BT[%02d]: ", i + Aarch32CpuContext::MaxStackTraceDepth / 2);
                FontManager::SetPosition(x + 72, FontManager::GetY());
                FontManager::PrintFormatLine("%08lX", bt_next);
            }
            
            FontManager::SetPosition(950, FontManager::GetY());
        }
    } else {
        //Registers
        FontManager::SetPosition(675, 64);
        FontManager::PrintLine("Arm64 Registers:");
        for (size_t i = 0; i < NumAarch64Gprs; i++) {
            u32 x = FontManager::GetX();
            FontManager::PrintFormat("%s:", Aarch64GprNames[i]);
            FontManager::SetPosition(x + 48, FontManager::GetY());
            FontManager::PrintFormatLine("%016lX", this->ctx->has_gprs[i] ? this->ctx->cpu_ctx.aarch64_ctx.x[i] : 0);
            FontManager::SetPosition(x, FontManager::GetY());
        }
        FontManager::Print("PC:   ");
        FontManager::PrintFormatLine("%016lX", this->ctx->cpu_ctx.aarch64_ctx.pc);
        //Backtrace
        FontManager::SetPosition(950, 64);
        FontManager::PrintLine("Start Address: ");
        FontManager::PrintFormatLine("%016lX", this->ctx->cpu_ctx.aarch64_ctx.start_address);
        FontManager::AddSpacingLines(0.5f);
        FontManager::PrintLine("Backtrace: ");
        for (u32 i = 0; i < Aarch64CpuContext::MaxStackTraceDepth / 2; i++) {
            u64 bt_cur = 0, bt_next = 0;
            if (i < this->ctx->cpu_ctx.aarch64_ctx.stack_trace_size) {
                bt_cur = this->ctx->cpu_ctx.aarch64_ctx.stack_trace[i];
            }
            if (i + Aarch64CpuContext::MaxStackTraceDepth / 2 < this->ctx->cpu_ctx.aarch64_ctx.stack_trace_size) {
                bt_next = this->ctx->cpu_ctx.aarch64_ctx.stack_trace[i + Aarch64CpuContext::MaxStackTraceDepth / 2];
            }
            
            if (i < this->ctx->cpu_ctx.aarch64_ctx.stack_trace_size) {
                u32 x = FontManager::GetX();
                FontManager::PrintFormat("BT[%02d]: ", i);
                FontManager::SetPosition(x + 72, FontManager::GetY());
                FontManager::PrintFormatLine("%016lX", bt_cur);
            }
            
            if (i + Aarch64CpuContext::MaxStackTraceDepth / 2 < this->ctx->cpu_ctx.aarch64_ctx.stack_trace_size) {
                u32 x = FontManager::GetX();
                FontManager::PrintFormat("BT[%02d]: ", i + Aarch64CpuContext::MaxStackTraceDepth / 2);
                FontManager::SetPosition(x + 72, FontManager::GetY());
                FontManager::PrintFormatLine("%016lX", bt_next);
            }
            
            FontManager::SetPosition(950, FontManager::GetY());
        }
    }


    /* Enqueue the buffer. */
    framebufferEnd(&fb);
    
    return rc;
}

Result ShowFatalTask::Run() {
    /* Don't show the fatal error screen until we've verified the battery is okay. */
    eventWait(this->battery_event, U64_MAX);

    return ShowFatal();
}

// Arbitrarily added strings.. will add more as i see which are more common.
std::string ShowFatalTask::getMeaning(u32 err) {
    std::string ret;
    switch(err) {
        case 0xE401:
            ret = "Invalid handle.";
            break;
        case 0x196002:
        case 0x196202:
        case 0x1A3E02:
        case 0x1A4002:
        case 0x1A4A02:
            ret = "Out of memory.";
            break;
        case 0x10801:
            ret = "Resource limit exceeded";
        case 0x1015:
            ret = "Permission denied";
        default:
            ret = "Unknown.";
            break;
    }
    return ret;
}

void BacklightControlTask::TurnOnBacklight() {
    lblSwitchBacklightOn(0);
}

Result BacklightControlTask::Run() {
    TurnOnBacklight();
    return 0;
}