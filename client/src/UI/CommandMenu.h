#pragma once

// DESVIACION: menú de comandos del DLL (tecla D).  El 0.97k original lo abre
// con la tecla D sobre el jugador seleccionado; acá es un panel compacto al
// centro-abajo con los tres pedidos que ya existen como comandos de chat
// (/trade, /party, /guild), sin tocar el protocolo.
class CCommandMenu {
public:
    void Toggle();
    bool IsOpen() const { return m_Open; }
    void Close() { m_Open = false; }
    void UpdateMouse();
    void Render();

private:
    bool Blocked() const;
    void Fire(int button);
    bool m_Open = false;
};
extern CCommandMenu gCommandMenu;
