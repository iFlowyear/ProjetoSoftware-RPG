# --- Estágio 1: Compilação do Servidor em C ---
FROM alpine:latest AS builder

# Instala o compilador GCC e as bibliotecas de desenvolvimento essenciais
RUN apk add --no-cache gcc musl-dev

WORKDIR /app

# Copia os arquivos do backend para a compilação
COPY backend/main.c backend/mongoose.c backend/mongoose.h ./

# Compila o binário otimizado de forma nativa para Linux com suporte a pthreads
RUN gcc main.c mongoose.c -o servidor_rpg -lpthread -D_GNU_SOURCE

# --- Estágio 2: Imagem de Execução Final ---
FROM alpine:latest

# Instala compatibilidade para a biblioteca padrão C
RUN apk add --no-cache libc6-compat

WORKDIR /app

# Copia o executável gerado no estágio anterior
COPY --from=builder /app/servidor_rpg .

# Copia o seu HTML otimizado e estruturado para a raiz
# Isso garante que a busca relativa do fopen("../index.html") funcione no Render
COPY index.html ./

# Se você tiver pastas adicionais como imagens/sons em uma pasta 'frontend', descomente a linha abaixo:
# COPY frontend/ ./frontend/

# Informa a porta padrão que o contêiner expõe (o Render gerencia isso internamente)
EXPOSE 8000

# Executa o servidor nativo em C
CMD ["./servidor_rpg"]