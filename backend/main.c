#include "mongoose.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>

// Macros para a Thread do servidor rodar isolada ao fundo
#ifdef _WIN32
#include <windows.h>
#define THREAD_RETURN DWORD WINAPI
#else
#include <pthread.h>
#include <unistd.h>
#define THREAD_RETURN void*
#endif

#define TAM 10
#define PORT_PADRAO "8000"

// Variáveis de controle EXCLUSIVAS da modalidade Web (Totalmente desconectadas do Terminal)
char mapaGlobal[TAM][TAM];
int heroiWebX, heroiWebY;
int chefeWebX, chefeWebY;
int bausGlobais = 3;
int ativoGlobal = 1;
char mensagemGlobal[128] = "Masmorra iniciada via Web!";

// Posições de trabalho do herói e chefe durante o jogo
int posHerX = -1, posHerY = -1;
int posChefX = -1, posChefY = -1;

// Protótipos das suas funções ORIGINAIS
void exibirMenu();
void inicializarMapa(char (*mapa)[TAM], int *hX, int *hY, int *cX, int *cY);
void desenharMapa(char (*mapa)[TAM], int baus);
void moverHeroi(char (*mapa)[TAM], int *heroiX, int *heroiY, int *chefeX, int *chefeY, int *baus, int *ativo, char comando);
void moverChefe(char (*mapa)[TAM], int heroiX, int heroiY, int *chefeX, int *chefeY, int baus, int *ativo);

// Handler HTTP: Gerencia o jogo online de forma autônoma
static void evento_http_handler(struct mg_connection *c, int ev, void *ev_data) {
    if (ev == MG_EV_HTTP_MSG) {
        struct mg_http_message *hm = (struct mg_http_message *) ev_data;

        if (mg_strcmp(hm->method, mg_str("OPTIONS")) == 0) {
            mg_http_reply(c, 204, 
                "Access-Control-Allow-Origin: *\r\n"
                "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
                "Access-Control-Allow-Headers: Content-Type\r\n", "");
            return;
        }

        // Rota 1: Envia o estado isolado da Web
        if (mg_match(hm->uri, mg_str("/api/state"), NULL)) {
            char mapa_json[1024] = "";
            strcat(mapa_json, "[");
            for(int i = 0; i < TAM; i++) {
                strcat(mapa_json, "\"");
                for(int j = 0; j < TAM; j++) {
                    char temp[2] = {mapaGlobal[i][j], '\0'};
                    strcat(mapa_json, temp);
                }
                strcat(mapa_json, (i == TAM - 1) ? "\"" : "\",");
            }
            strcat(mapa_json, "]");

            mg_http_reply(c, 200, 
                "Content-Type: application/json; charset=utf-8\r\n"
                "Access-Control-Allow-Origin: *\r\n", 
                "{ \"map\": %s, \"chests\": %d, \"active\": %d, \"msg\": \"%s\" }", 
                mapa_json, bausGlobais, ativoGlobal, mensagemGlobal);
        } 
        // Rota 2: Movimentos aplicados apenas no ambiente online
        else if (mg_match(hm->uri, mg_str("/api/move"), NULL)) {
            char dir[2] = {0};
            mg_http_get_var(&hm->query, "dir", dir, sizeof(dir));
            
            if (posHerX == -1) {
                for(int i=0; i<TAM; i++) {
                    for(int j=0; j<TAM; j++) {
                        if (mapaGlobal[i][j] == 'H') { posHerX = j; posHerY = i; }
                        if (mapaGlobal[i][j] == 'X') { posChefX = j; posChefY = i; }
                    }
                }
            }

            if (ativoGlobal && dir[0] != '\0') {
                int bAntes = bausGlobais;
                moverHeroi(mapaGlobal, &posHerX, &posHerY, &posChefX, &posChefY, &bausGlobais, &ativoGlobal, dir[0]);
                
                if (bausGlobais < bAntes) {
                    strcpy(mensagemGlobal, "Você coletou um baú de tesouro!");
                } else {
                    strcpy(mensagemGlobal, "Você se moveu pelas salas da masmorra.");
                }
                
                if (!ativoGlobal) {
                    if (bausGlobais > 0) {
                        strcpy(mensagemGlobal, "GAME OVER! O Chefe te pegou antes que você pudesse coletar os baús!");
                    } else {
                        strcpy(mensagemGlobal, "VITÓRIA! O Chefe te atacou, mas seu herói estava forte com os tesouros e o venceu!");
                    }
                }
            }
            mg_http_reply(c, 200, "Access-Control-Allow-Origin: *\r\n", "ok");
        }
        // Rota 3: Reset do jogo (reinicializa o mapa)
        else if (mg_match(hm->uri, mg_str("/api/reset"), NULL)) {
            inicializarMapa(mapaGlobal, &heroiWebX, &heroiWebY, &chefeWebX, &chefeWebY);
            posHerX = -1;
            posHerY = -1;
            posChefX = -1;
            posChefY = -1;
            bausGlobais = 3;
            ativoGlobal = 1;
            strcpy(mensagemGlobal, "A masmorra desperta... Mova-se com WASD.");
            mg_http_reply(c, 200, 
                "Content-Type: application/json; charset=utf-8\r\n"
                "Access-Control-Allow-Origin: *\r\n", 
                "{ \"status\": \"reset\" }");
        } 
        // Rota 4: Carrega o index.html da raiz
        else if (mg_match(hm->uri, mg_str("/"), NULL)) {
            FILE *f = fopen("../index.html", "r");
            if (f != NULL) {
                fseek(f, 0, SEEK_END);
                long fsize = ftell(f);
                fseek(f, 0, SEEK_SET);
                char *html = malloc(fsize + 1);
                fread(html, 1, fsize, f);
                fclose(f);
                html[fsize] = 0;
                mg_http_reply(c, 200, "Content-Type: text/html; charset=utf-8\r\n", "%s", html);
                free(html);
            } else {
                mg_http_reply(c, 404, "Content-Type: text/plain; charset=utf-8\r\n", "Erro 404: index.html nao encontrado.");
            }
        } else {
            struct mg_http_serve_opts opts = {.root_dir = ".."};
            mg_http_serve_dir(c, hm, &opts);
        }
    }
}

// Thread secundária dedicada a manter o ecossistema Web ativo
THREAD_RETURN rodar_servidor_web(void *arg) {
    struct mg_mgr *mgr = (struct mg_mgr *)arg;
    for (;;) {
        mg_mgr_poll(mgr, 20);
    }
    return 0;
}

int main() {
#ifdef _WIN32
    SetConsoleOutputCP(65001);
    SetConsoleCP(65001);
#endif
    
    srand(time(NULL));

    // Desativa os prints internos de log da Mongoose para manter o terminal limpo
    mg_log_set(MG_LL_NONE);

    // Inicialização da matriz própria da Web na memória
    inicializarMapa(mapaGlobal, &heroiWebX, &heroiWebY, &chefeWebX, &chefeWebY);

    // Inicialização da Mongoose em background
    struct mg_mgr mgr;
    mg_mgr_init(&mgr);
    
    // SISTEMA DE DETECÇÃO ADAPTADO DO SEU EXEMPLO: Local vs Nuvem
    const char *port = getenv("PORT");
    char listen_addr[64];
    if (port == NULL) {
        port = PORT_PADRAO;
        // No computador local, limita ao IP de loopback seguro
        snprintf(listen_addr, sizeof(listen_addr), "http://127.0.0.1:%s", port);
    } else {
        // Na nuvem (Render), escuta em todas as interfaces para receber tráfego externo
        snprintf(listen_addr, sizeof(listen_addr), "http://0.0.0.0:%s", port);
    }
    
    mg_http_listen(&mgr, listen_addr, evento_http_handler, NULL);
    
#ifdef _WIN32
    CreateThread(NULL, 0, rodar_servidor_web, &mgr, 0, NULL);
#else
    pthread_t thread_id;
    pthread_create(&thread_id, NULL, rodar_servidor_web, &mgr);
#endif

    printf("====================================================\n");
    printf("   MASMORRA ENGINE v3.5 INDEPENDENT ONLINE          \n");
    printf("   Servidor Web Ativo em: %s                        \n", listen_addr);
    printf("====================================================\n\n");

    // Variáveis locais da main (evitando globais usando ponteiros) - AS SUAS ORIGINAIS
    char mapa[TAM][TAM];
    int heroiX, heroiY;
    int chefeX, chefeY;
    int bausRestantes = 3;
    int jogoAtivo = 1;
    
    // 1. Exibe o menu explicativo e as instruções antes do jogo começar
    exibirMenu();
    
    // 2. Inicialização do mapa passando as referências por ponteiro
    inicializarMapa(mapa, &heroiX, &heroiY, &chefeX, &chefeY);

    char tecla;
    
    // Loop principal do Dungeon Crawler - O SEU ORIGINAL COM SCANF
    while (jogoAtivo) {
        desenharMapa(mapa, bausRestantes);
        
        printf("\nComando (W/A/S/D) ou 'Q' para sair: ");
        scanf(" %c", &tecla);
        
        if (tecla == 'Q' || tecla == 'q') {
            printf("\nVocê saiu do jogo. Masmorra encerrada!\n");
            break;
        }
        
        // Função de controle de movimento utilizando ponteiros para os índices X e Y
        moverHeroi(mapa, &heroiX, &heroiY, &chefeX, &chefeY, &bausRestantes, &jogoAtivo, tecla);
    }

    // Mantém a Thread de rede aberta caso o terminal encerre ou finalize o jogo local
    while(1) {
#ifdef _WIN32
        Sleep(1000);
#else
        sleep(1);
#endif
    }

    mg_mgr_free(&mgr);
    return 0;
}

// Exibe a tela inicial com o funcionamento do jogo e personagens - EXATAMENTE A SUA ORIGINAL
void exibirMenu() {
    printf("==================================================\n");
    printf("               BEM-VINDO À MASMORRA               \n");
    printf("==================================================\n\n");
    
    printf("--- COMO JOGAR ---\n");
    printf(" * Seu objetivo é coletar todos os baús do mapa.\n");
    printf(" * Após coletar todos os baús, você ganha força para\n");
    printf("   enfrentar o Chefe e vencer o jogo.\n");
    printf(" * Cuidado! Se o Chefe te pegar antes de você pegar\n");
    printf("   todos os baús, é GAME OVER!\n\n");

    printf("--- PERSONAGENS E ITENS ---\n");
    printf(" [ H ] -> Herói (Você)\n");
    printf(" [ X ] -> Chefe (Inimigo que te persegue)\n");
    printf(" [ T ] -> Baú (Tesouro a ser coletado)\n");
    printf(" [ # ] -> Parede / Obstáculo\n");
    printf(" [ . ] -> Caminho livre\n\n");

    printf("--- CONTROLES ---\n");
    printf(" W - Mover para CIMA\n");
    printf(" S - Mover para BAIXO\n");
    printf(" A - Mover para ESQUERDA\n");
    printf(" D - Mover para DIREITA\n");
    printf(" Q - Sair do jogo\n\n");
    
    printf("Pressione ENTER para começar o mapa no terminal... ");
    fflush(stdin); // Limpa o lixo do teclado
    getchar();     // Captura o ENTER puro sozinho
    
    // Limpa a tela do console antes de desenhar o mapa pela primeira vez
#ifdef _WIN32
    system("cls");
#else
    system("clear");
#endif
}

// Preenche as bordas com '#' e sorteia os elementos nas posições vazias '.' - EXATAMENTE A SUA ORIGINAL
void inicializarMapa(char (*mapa)[TAM], int *hX, int *hY, int *cX, int *cY) {
    for (int i = 0; i < TAM; i++) {
        for (int j = 0; j < TAM; j++) {
            if (i == 0 || i == TAM - 1 || j == 0 || j == TAM - 1) {
                mapa[i][j] = '#'; 
            } else {
                mapa[i][j] = '.'; 
            }
        }
    }

    int x, y;

    for (int i = 0; i < 3; i++) {
        do {
            y = (rand() % (TAM - 2)) + 1;
            x = (rand() % (TAM - 2)) + 1;
        } while (mapa[y][x] != '.');
        mapa[y][x] = 'T';
    }

    do {
        y = (rand() % (TAM - 2)) + 1;
        x = (rand() % (TAM - 2)) + 1;
    } while (mapa[y][x] != '.');
    mapa[y][x] = 'X';
    *cX = x;
    *cY = y;

    do {
        y = (rand() % (TAM - 2)) + 1;
        x = (rand() % (TAM - 2)) + 1;
    } while (mapa[y][x] != '.' || (x == *cX && y == *cY));
    mapa[y][x] = 'H';
    *hX = x;
    *hY = y;
}

// Desenha a matriz em texto na tela sem recriá-la - EXATAMENTE A SUA ORIGINAL
void desenharMapa(char (*mapa)[TAM], int baus) {
    printf("\n--- GERADOR DE MASMORRAS (Baús restantes: %d) ---\n", baus);
    for (int i = 0; i < TAM; i++) {
        for (int j = 0; j < TAM; j++) {
            printf("%c ", mapa[i][j]);
        }
        printf("\n");
    }
}

// Função de controle de movimento por referência (ponteiros para X e Y) - EXATAMENTE A SUA ORIGINAL
void moverHeroi(char (*mapa)[TAM], int *heroiX, int *heroiY, int *chefeX, int *chefeY, int *baus, int *ativo, char comando) {
    int proxX = *heroiX;
    int proxY = *heroiY;

    switch (comando) {
        case 'W': case 'w': proxY--; break;
        case 'S': case 's': proxY++; break;
        case 'A': case 'a': proxX--; break;
        case 'D': case 'd': proxX++; break;
        default: return; 
    }

    if (mapa[proxY][proxX] != '#') {
        
        if (mapa[proxY][proxX] == 'T') {
            (*baus)--;
        }

        if (proxX == *chefeX && proxY == *chefeY) {
            if (*baus > 0) {
                printf("\nGAME OVER! Você enfrentou o Chefe sem coletar todos os baús!\n");
            } else {
                printf("\nVITÓRIA! Você coletou todos os baús e derrotou o Chefe! PARABÉNS!\n");
            }
            *ativo = 0;
            return;
        }

        mapa[*heroiY][*heroiX] = '.';
        *heroiX = proxX;
        *heroiY = proxY;
        mapa[*heroiY][*heroiX] = 'H';

        moverChefe(mapa, *heroiX, *heroiY, chefeX, chefeY, *baus, ativo);
    }
}

// IA do Chefe que o faz andar em direção ao jogador pelas coordenadas X e Y - EXATAMENTE A SUA ORIGINAL
void moverChefe(char (*mapa)[TAM], int heroiX, int heroiY, int *chefeX, int *chefeY, int baus, int *ativo) {
    if (rand() % 2 == 0) return;

    int proxChefeX = *chefeX;
    int proxChefeY = *chefeY;

    if (*chefeX != heroiX) {
        if (*chefeX < heroiX) proxChefeX++;
        else proxChefeX--;
    } 
    else if (*chefeY != heroiY) {
        if (*chefeY < heroiY) proxChefeY++;
        else proxChefeY--;
    }

    if (mapa[proxChefeY][proxChefeX] != '#' && mapa[proxChefeY][proxChefeX] != 'T') {
        mapa[*chefeY][*chefeX] = '.';
        
        *chefeX = proxChefeX;
        *chefeY = proxChefeY;
        
        if (*chefeX == heroiX && *chefeY == heroiY) {
            if (baus > 0) {
                printf("\nGAME OVER! O Chefe te pegou antes que você pudesse coletar os baús!\n");
            } else {
                printf("\nVITÓRIA! O Chefe te atacou, mas seu herói estava forte com os tesouros e o venceu!\n");
            }
            *ativo = 0;
        } else {
            mapa[*chefeY][*chefeX] = 'X';
        }
    }
}