#pragma once

// DESVIACION: ventana de historial de chat expandible (tecla L).  Muestra las
// líneas ya guardadas por el widget de chat (ChatListBox, lista principal,
// de la más nueva a la más vieja) en una ventana grande con scroll; no guarda
// nada propio ni toca el protocolo.
class CChatLogWindow {
public:
    void Toggle();
    bool IsOpen() const { return m_Open; }
    void Close() { m_Open = false; }
    void UpdateMouse();
    void Render();
    void ScrollWheel(int notches);  // >0: hacia los mensajes viejos
    void ScrollPages(int pages);    // >0: una página hacia los viejos (PageUp)

private:
    bool Blocked() const;
    int VisibleRows() const;
    int MaxScroll() const;
    void ClampScroll();
    bool m_Open = false;
    int m_Scroll = 0;  // líneas omitidas desde la más nueva (0 = pegado al final)
};
extern CChatLogWindow gChatLogWindow;
