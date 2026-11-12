#include "stdafx.h"
#include "Render/Gfx.h"
#include "Platform/Text.h"
#include "UI/ChatLogWindow.h"
#include "UI/ChatListBox.h"
#include "UI/CommandMenu.h"
#include "UI/MiniMap.h"
#include "UI/MoveList.h"
#include "UI/UIState.h"
#include "Core/Font.h"
#include "Core/Window.h"
#include "Local/ClientText.h"

CChatLogWindow gChatLogWindow;
namespace {
// Ventana ancha sobre la zona del chat clásico, apoyada en y=424 como el
// menú D.  El alto de fila (13) es el de las líneas del chat.
struct ChatLogLayout {
    static int X() { return gWindow.PanelOrigin(Ui::WindowLeft) + 5; }
    static constexpr int Width = 420;
    static constexpr int Y = 150, Height = 274;
    static constexpr int TitleOffset = 6;
    static constexpr int RowsOffset = 26;
    static constexpr int RowHeight = 13;
    static constexpr int TextX = 8, TextWidth = Width - 34;
    static constexpr int CloseSize = 16;
    static int CloseX() { return X() + Width - CloseSize - 4; }
    static constexpr int CloseY = Y + 4;
    static constexpr int ArrowWidth = 16, ArrowHeight = 20;
    static int ArrowX() { return X() + Width - ArrowWidth - 2; }
    static constexpr int UpY = Y + RowsOffset;
    static constexpr int DownY = Y + Height - ArrowHeight - 4;
    static constexpr int TrackY = UpY + ArrowHeight + 2;
    static constexpr int TrackHeight = DownY - TrackY - 2;
};
using Layout = ChatLogLayout;
bool Inside(int x, int y, int w, int h)
{
    return Ui::Rect{x, y, w, h}.Contains((int)MouseX, (int)MouseY);
}
void Rect(int x, int y, int w, int h, float r, float g, float b, float a)
{
    EnableAlphaTest(true);
    Gfx::Color4f(r, g, b, a);
    GL_DrawRect((float)x, (float)y, (float)w, (float)h);
    GL_ResetState();
    Gfx::Color4f(1, 1, 1, 1);
}
void TextCentered(int x, int y, const char* text, int width)
{
    RenderText(x, y, const_cast<char*>(text),
        (int)(width * gWindow.GetWidth() / 640), 1, nullptr);
}
void TextLeft(int x, int y, const char* text, int width)
{
    RenderText(x, y, const_cast<char*>(text),
        (int)(width * gWindow.GetWidth() / 640), 0, nullptr);
}
// Paleta por canal de ChatLB_renderLine (ChatListBox.cpp), aclarada donde el
// original usa texto oscuro sobre franja (0/2) para que se lea sobre el fondo.
DWORD KindColor(int kind)
{
    switch (kind) {
        case 1: return 0xFFFF9664;
        case 2: return 0xFF6E8CFF;
        case 3: return 0xFFC8DCE6;
        case 4: return 0xFFFFC800;
        case 5: return 0xFF96FF00;
        default: return 0xFFFFFFFF;
    }
}
}

bool CChatLogWindow::Blocked() const
{
    return !UIState::CanOpenInformationalPanel();
}

int CChatLogWindow::VisibleRows() const
{
    return (Layout::Height - Layout::RowsOffset - 4) / Layout::RowHeight;
}

int CChatLogWindow::MaxScroll() const
{
    const int extra = ChatListBox_HistoryCount() - VisibleRows();
    return extra > 0 ? extra : 0;
}

void CChatLogWindow::ClampScroll()
{
    const int maxScroll = MaxScroll();
    if (m_Scroll > maxScroll) m_Scroll = maxScroll;
    if (m_Scroll < 0) m_Scroll = 0;
}

void CChatLogWindow::Toggle()
{
    if (Blocked()) { m_Open = false; return; }
    // Comparte pantalla con M, Tab y D: se cierran mutuamente, como ellos.
    if (!m_Open) {
        gMoveList.Close();
        gMiniMap.Close();
        gCommandMenu.Close();
        m_Scroll = 0;
    }
    m_Open = !m_Open;
    PlayBuffer(25, 0, 0);
}

void CChatLogWindow::ScrollWheel(int notches)
{
    if (!m_Open) return;
    m_Scroll += notches * 3;
    ClampScroll();
}

void CChatLogWindow::ScrollPages(int pages)
{
    if (!m_Open) return;
    m_Scroll += pages * VisibleRows();
    ClampScroll();
    PlayBuffer(25, 0, 0);
}

void CChatLogWindow::UpdateMouse()
{
    if (Blocked()) { m_Open = false; return; }
    if (!m_Open) return;
    const int x = Layout::X();
    if (!Inside(x, Layout::Y, Layout::Width, Layout::Height)) {
        // DESVIACION: clic fuera = cerrar, sin consumir el clic (sigue al juego).
        if (MouseLButtonPush) Close();
        return;
    }
    UIState::CaptureMouseForUI();
    const bool click = MouseLButtonPush != 0;
    MouseLButton = 0;
    MouseLButtonPush = 0;
    MouseLButtonPop = 0;
    if (!click) return;
    if (Inside(Layout::CloseX(), Layout::CloseY, Layout::CloseSize, Layout::CloseSize)) {
        Close();
        PlayBuffer(25, 0, 0);
        return;
    }
    if (MaxScroll() > 0) {
        if (Inside(Layout::ArrowX(), Layout::UpY, Layout::ArrowWidth, Layout::ArrowHeight)) {
            ScrollPages(1);
            return;
        }
        if (Inside(Layout::ArrowX(), Layout::DownY, Layout::ArrowWidth, Layout::ArrowHeight)) {
            ScrollPages(-1);
            return;
        }
    }
}

void CChatLogWindow::Render()
{
    if (!m_Open || Blocked()) return;
    ClampScroll();
    const int x = Layout::X();
    const int count = ChatListBox_HistoryCount();
    const int rows = VisibleRows();
    const DWORD color = m_dwTextColor, back = m_dwBackColor;
    HGDIOBJ font = gFont.SelectFont(gFont.GetTextDC(), gFont.GetFont(FONT_NORMAL));
    m_dwBackColor = 0;
    Rect(x, Layout::Y, Layout::Width, Layout::Height, 0, 0, 0, .8f);
    m_dwTextColor = 0xFF00FFFF;
    TextCentered(x, Layout::Y + Layout::TitleOffset,
                 gClientText.Get(ClientTextId::ChatLogTitle), Layout::Width);
    // Cierre rojo como el del menú M, en la esquina del título.
    const bool closeHover = Inside(Layout::CloseX(), Layout::CloseY,
                                   Layout::CloseSize, Layout::CloseSize);
    Rect(Layout::CloseX(), Layout::CloseY, Layout::CloseSize, Layout::CloseSize,
         closeHover ? 1.0f : .8f, 0, 0, 1);
    m_dwTextColor = 0xFFFFFFFF;
    TextCentered(Layout::CloseX(), Layout::CloseY + 1, "X", Layout::CloseSize);
    if (!count) {
        m_dwTextColor = 0xFFFFFFFF;
        TextCentered(x, Layout::Y + Layout::RowsOffset,
                     gClientText.Get(ClientTextId::ChatLogEmpty), Layout::Width);
    }
    char name[48], text[300], line[384];
    for (int r = 0; r < rows; ++r) {
        // La fila inferior es la línea visible más nueva (índice m_Scroll).
        const int index = m_Scroll + (rows - 1 - r);
        if (index >= count) continue;
        const int kind = ChatListBox_HistoryLine(index, name, sizeof(name),
                                                 text, sizeof(text));
        if (kind < 0) continue;
        if (name[0])
            Platform::TextFormatSecure(line, sizeof(line), "%s: %s", name, text);
        else
            Platform::TextFormatSecure(line, sizeof(line), "%s", text);
        m_dwTextColor = KindColor(kind);
        TextLeft(x + Layout::TextX, Layout::Y + Layout::RowsOffset + r * Layout::RowHeight,
                 line, Layout::TextWidth);
    }
    if (count > rows) {
        const bool upHover = Inside(Layout::ArrowX(), Layout::UpY,
                                    Layout::ArrowWidth, Layout::ArrowHeight);
        const bool downHover = Inside(Layout::ArrowX(), Layout::DownY,
                                      Layout::ArrowWidth, Layout::ArrowHeight);
        Rect(Layout::ArrowX(), Layout::UpY, Layout::ArrowWidth, Layout::ArrowHeight,
             upHover ? .8f : .3f, upHover ? .8f : .3f, .1f, .6f);
        Rect(Layout::ArrowX(), Layout::DownY, Layout::ArrowWidth, Layout::ArrowHeight,
             downHover ? .8f : .3f, downHover ? .8f : .3f, .1f, .6f);
        m_dwTextColor = 0xFFFFFFFF;
        TextCentered(Layout::ArrowX(), Layout::UpY + 3, "^", Layout::ArrowWidth);
        TextCentered(Layout::ArrowX(), Layout::DownY + 3, "v", Layout::ArrowWidth);
        // Pulgar de la barra: posición proporcional al recorrido.
        const int maxScroll = MaxScroll();
        const int thumbHeight = Layout::TrackHeight * rows / count > 12
            ? Layout::TrackHeight * rows / count : 12;
        const int travel = Layout::TrackHeight - thumbHeight;
        const int thumbY = maxScroll > 0
            ? Layout::TrackY + travel * (maxScroll - m_Scroll) / maxScroll
            : Layout::TrackY;
        Rect(Layout::ArrowX() + 5, Layout::TrackY, 6, Layout::TrackHeight,
             .2f, .2f, .2f, .5f);
        Rect(Layout::ArrowX() + 5, thumbY, 6, thumbHeight, .9f, .9f, .9f, .8f);
    }
    gFont.SelectFont(gFont.GetTextDC(), font);
    m_dwTextColor = color;
    m_dwBackColor = back;
    Gfx::Color4f(1, 1, 1, 1);
}
