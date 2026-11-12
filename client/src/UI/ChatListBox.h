// ChatListBox.h — engine chat-widget object behind dword_55C9FF0.
// See ChatListBox.cpp for the rationale and full vtable layout.
#pragma once

extern "C" {
// Allocate, default-construct, return the chat-widget object.  Caller stores
// the pointer into DAT_055c9ff0.  Returns NULL on OOM.
void* ChatListBox_Construct(void);

// Vtable del widget. El puntero vive en una cabecera nativa delante del objeto.
void** ChatWidget_GetVTable(void* widget);

// Widget hover flag — set by ChatListBox per-frame input handling, consumed
// by cursor render.  (Storage in ChatListBox.cpp.)
extern int MouseOnWindow;

// Read-only access to the stored chat history (list2 of the main widget,
// newest first) for the expandable chat window (tecla L).  The layout stays
// private to ChatListBox.cpp; callers only get copies.
// ChatListBox_HistoryLine returns the line kind (0..5) or -1 if the index
// is out of range; index 0 is the newest line.
int ChatListBox_HistoryCount(void);
int ChatListBox_HistoryLine(int index, char* name, int nameCapacity,
                            char* text, int textCapacity);
}
