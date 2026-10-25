// Identifiant du titre (CUSA00069…) lu dans un param.sfo, et test « est-ce un jeu ».
// Code C pur, sans dépendance PS4, pour pouvoir le tester sur Mac/PC.
#pragma once

#include <stddef.h>
#include <stdint.h>

// Copie TITLE_ID (9 caractères, terminé par '\0') dans out. Retourne 0 si trouvé, -1 sinon
// (fichier tronqué, pas un param.sfo, clé absente).
int a4f_sfo_title_id(const uint8_t *sfo, size_t len, char out[16]);

// 1 pour un jeu ou une appli du store (CUSA, PCAS, PLAS), 0 pour le reste (homebrews, système).
int a4f_is_game_title(const char *title_id);
