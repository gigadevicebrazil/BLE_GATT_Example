/*!
    \file    main.c
    \brief   BLE datatrans server demo adapted as BLE terminal for 3 LEDs.

    Android writes commands through BLE. The board controls LEDs and replies by
    BLE notification, so the response appears in the Android BLE terminal.

    Commands:
        L1      -> LED1 ON  (PB0)
        L2      -> LED2 ON  (PA12)
        L3      -> LED3 ON  (PB4)
        D1      -> LED1 OFF
        D2      -> LED2 OFF
        D3      -> LED3 OFF
        T1      -> toggle LED1
        T2      -> toggle LED2
        T3      -> toggle LED3
        ON      -> all ON
        OFF     -> all OFF
        STATUS  -> LED states
        HELP    -> command list
*/

#include <stdint.h>   /* Tipos inteiros com tamanho fixo, como uint8_t e uint16_t. */
#include <stdbool.h>  /* Tipo booleano bool, true e false. */
#include <stdio.h>    /* snprintf, usado para montar strings de resposta. */
#include <string.h>   /* strlen, strcmp e memcpy, usados no tratamento de strings e buffers. */
#include <ctype.h>    /* toupper, usado para aceitar comandos em letras minúsculas ou maiúsculas. */

/* Includes específicos do SDK/SoC GD32VW55x e da pilha BLE. */
#include "dbg_print.h"
#include "gd32vw55x.h"
#include "gd32vw55x_platform.h"
#include "wrapper_os.h"
#include "ble_adapter.h"
#include "ble_adv.h"
#include "ble_conn.h"
#include "ble_utils.h"
#include "ble_export.h"
#include "ble_sec.h"
#include "ble_datatrans_srv.h"

/* ===================== Device name ===================== */
/* Nome anunciado pelo dispositivo BLE. É o nome visto no aplicativo Android. */
#define DEV_NAME "GD32-BLE-LED"

/* Tamanho do nome do dispositivo. Usa strlen sobre a variável dev_name definida abaixo. */
#define DEV_NAME_LEN strlen(dev_name)

/* ===================== LED configuration ===================== */
/*
 * Mapeamento físico dos LEDs usados pelo exemplo.
 * Cada LED possui:
 * - PORT: periférico GPIO onde o pino está localizado;
 * - PIN: pino específico usado;
 * - RCU: clock do GPIO que precisa ser habilitado antes de configurar o pino.
 */
#define LED1_PORT       GPIOB
#define LED1_PIN        GPIO_PIN_0
#define LED1_RCU        RCU_GPIOB

#define LED2_PORT       GPIOA
#define LED2_PIN        GPIO_PIN_12
#define LED2_RCU        RCU_GPIOA

#define LED3_PORT       GPIOB
#define LED3_PIN        GPIO_PIN_4
#define LED3_RCU        RCU_GPIOB

/*
 * Ajuste conforme o hardware da placa:
 * 0 = LED acende com nível lógico alto no pino.
 * 1 = LED acende com nível lógico baixo no pino.
 *
 * Neste código está configurado como 0, então setar o GPIO liga o LED
 * e resetar o GPIO desliga o LED.
 */
#define LED_ACTIVE_LOW  0

/* ===================== Advertising parameters ===================== */
/*
 * Estrutura usada pela aplicação para guardar informações do advertising BLE:
 * - adv_idx: índice/identificador do conjunto de advertising criado pela pilha BLE;
 * - adv_state: estado atual do advertising, usado para controlar a sequência criar/iniciar.
 */
typedef struct
{
    uint8_t         adv_idx;
    ble_adv_state_t adv_state;
} app_adv_param_t;

/* Definitions of the different task priorities */
/*
 * Prioridades das tarefas criadas pela pilha BLE e pela aplicação BLE.
 * OS_TASK_PRIORITY converte o nível lógico de prioridade para o formato usado pelo RTOS/wrapper.
 */
enum
{
    BLE_STACK_TASK_PRIORITY = OS_TASK_PRIORITY(2),
    BLE_APP_TASK_PRIORITY   = OS_TASK_PRIORITY(1),
};

/* Definitions of the different BLE task stack size requirements */
/*
 * Tamanhos de stack, em unidades esperadas pelo SDK, para as tarefas BLE.
 * A pilha BLE geralmente precisa de mais stack que a tarefa de aplicação BLE.
 */
enum
{
    BLE_STACK_TASK_STACK_SIZE = 768,
    BLE_APP_TASK_STACK_SIZE   = 512,
};

/* Nome real usado pela pilha BLE. O macro DEV_NAME_LEN depende desta variável. */
char dev_name[] = {DEV_NAME};

/* Ambiente global de advertising. Inicializado zerado. */
static app_adv_param_t app_adv_env = {0};

/*
 * Índice da conexão BLE ativa.
 * 0xFF é usado como valor inválido, indicando que não existe conexão ativa.
 */
static uint8_t conn_idx = 0xFF;

/* Flag simples para indicar se existe uma conexão BLE ativa. */
static uint8_t ble_connected = 0;

/*
 * Estados lógicos dos LEDs.
 * 0 = desligado.
 * 1 = ligado.
 * A função leds_apply() converte estes estados para nível elétrico no GPIO.
 */
static uint8_t led1_state = 0;
static uint8_t led2_state = 0;
static uint8_t led3_state = 0;

/* Forward declaration required because ble_init registers this callback. */
/*
 * Protótipo do callback de recepção BLE.
 * Ele é declarado antes porque ble_init() registra esta função antes da sua definição no arquivo.
 */
void app_datatrans_srv_rx_callback(uint8_t conn_idx, uint16_t data_len, uint8_t *p_data);

/* ===================== LED helpers ===================== */
/*
 * Escreve o estado físico em um GPIO de LED.
 *
 * Parâmetros:
 * - gpio_periph: porta GPIO, por exemplo GPIOA ou GPIOB;
 * - pin: pino dentro da porta;
 * - on: estado lógico desejado, onde 1 significa LED ligado e 0 LED desligado.
 *
 * A função respeita LED_ACTIVE_LOW. Assim, o restante do código trabalha sempre
 * com a ideia lógica de ligado/desligado, sem depender se o LED é ativo alto ou ativo baixo.
 */
static void led_write(uint32_t gpio_periph, uint32_t pin, uint8_t on)
{
#if LED_ACTIVE_LOW
    /* Em hardware ativo baixo, nível baixo liga o LED e nível alto desliga. */
    if (on) {
        gpio_bit_reset(gpio_periph, pin);
    } else {
        gpio_bit_set(gpio_periph, pin);
    }
#else
    /* Em hardware ativo alto, nível alto liga o LED e nível baixo desliga. */
    if (on) {
        gpio_bit_set(gpio_periph, pin);
    } else {
        gpio_bit_reset(gpio_periph, pin);
    }
#endif
}

/*
 * Aplica nos pinos físicos os estados armazenados em led1_state, led2_state e led3_state.
 * Esta função centraliza a atualização dos LEDs após qualquer comando recebido por BLE.
 */
static void leds_apply(void)
{
    led_write(LED1_PORT, LED1_PIN, led1_state);
    led_write(LED2_PORT, LED2_PIN, led2_state);
    led_write(LED3_PORT, LED3_PIN, led3_state);
}

/*
 * Inicializa os três GPIOs usados como saída para os LEDs.
 *
 * Sequência:
 * 1. Habilita o clock das portas GPIO usadas;
 * 2. Configura cada pino como saída digital;
 * 3. Configura saída push-pull com velocidade de 10 MHz;
 * 4. Inicializa os estados lógicos dos LEDs como desligados;
 * 5. Aplica esse estado inicial nos pinos físicos.
 */
static void leds_init(void)
{
    rcu_periph_clock_enable(LED1_RCU);
    rcu_periph_clock_enable(LED2_RCU);
    rcu_periph_clock_enable(LED3_RCU);

    gpio_mode_set(LED1_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, LED1_PIN);
    gpio_output_options_set(LED1_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_10MHZ, LED1_PIN);

    gpio_mode_set(LED2_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, LED2_PIN);
    gpio_output_options_set(LED2_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_10MHZ, LED2_PIN);

    gpio_mode_set(LED3_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, LED3_PIN);
    gpio_output_options_set(LED3_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_10MHZ, LED3_PIN);

    led1_state = 0;
    led2_state = 0;
    led3_state = 0;
    leds_apply();
}

/* ===================== BLE terminal helpers ===================== */
/*
 * Envia uma string para o celular por meio do serviço BLE datatrans.
 *
 * A resposta será enviada como notificação/transferência BLE para aparecer
 * no terminal BLE do Android, desde que o celular esteja conectado.
 */
static void ble_terminal_send(const char *text)
{
    uint16_t len;

    /* Proteção contra ponteiro nulo. */
    if (text == NULL) {
        return;
    }

    /* Sem conexão BLE válida, não há para quem enviar a resposta. */
    if (!ble_connected || conn_idx == 0xFF) {
        app_print("BLE not connected. Response not sent: %s", text);
        return;
    }

    /* Calcula o tamanho da string a ser enviada. */
    len = (uint16_t)strlen(text);
    if (len == 0U) {
        return;
    }

    /* Envia os bytes pelo serviço BLE datatrans. O retorno é descartado intencionalmente. */
    (void)ble_datatrans_srv_tx(conn_idx, (uint8_t *)text, len);
}

/*
 * Monta e envia uma mensagem com o estado atual dos três LEDs.
 * A resposta segue o formato:
 * "led1 ligado/desligado, led2 ligado/desligado, led3 ligado/desligado".
 */
static void ble_terminal_send_status(void)
{
    char rsp[80];

    (void)snprintf(rsp, sizeof(rsp),
                   "led1 %s, led2 %s, led3 %s\r\n",
                   led1_state ? "ligado" : "desligado",
                   led2_state ? "ligado" : "desligado",
                   led3_state ? "ligado" : "desligado");
    ble_terminal_send(rsp);
}

/*
 * Normaliza o comando recebido pelo BLE antes da comparação.
 *
 * O que esta função faz:
 * - remove '\r', '\n', espaços e tabulações;
 * - converte letras para maiúsculas;
 * - compacta o texto no próprio buffer cmd.
 *
 * Com isso, comandos como "l1", "L1\r\n" ou " L1 " viram simplesmente "L1".
 */
static void normalize_command(char *cmd)
{
    size_t i;
    size_t wr = 0;

    if (cmd == NULL) {
        return;
    }

    for (i = 0; cmd[i] != '\0'; i++) {
        unsigned char c = (unsigned char)cmd[i];

        if (c == '\r' || c == '\n' || c == ' ' || c == '\t') {
            continue;
        }

        cmd[wr++] = (char)toupper(c);
    }

    cmd[wr] = '\0';
}

/*
 * Interpreta o comando recebido pelo celular e executa a ação correspondente.
 *
 * Comandos aceitos:
 * - L1, L2, L3: liga o LED correspondente;
 * - D1, D2, D3: desliga o LED correspondente;
 * - T1, T2, T3: inverte o estado do LED correspondente;
 * - ON: liga todos os LEDs;
 * - OFF: desliga todos os LEDs;
 * - STATUS: envia o estado atual dos LEDs;
 * - HELP ou ?: envia a lista de comandos disponíveis.
 */
static void process_ble_command(char *cmd)
{
    /* Limpa e padroniza o comando antes de compará-lo. */
    normalize_command(cmd);
    app_print("BLE command: %s\r\n", cmd);

    if (strcmp(cmd, "L1") == 0) {
        led1_state = 1;
        leds_apply();
        ble_terminal_send("led1 ligado\r\n");
    } else if (strcmp(cmd, "L2") == 0) {
        led2_state = 1;
        leds_apply();
        ble_terminal_send("led2 ligado\r\n");
    } else if (strcmp(cmd, "L3") == 0) {
        led3_state = 1;
        leds_apply();
        ble_terminal_send("led3 ligado\r\n");
    } else if (strcmp(cmd, "D1") == 0) {
        led1_state = 0;
        leds_apply();
        ble_terminal_send("led1 desligado\r\n");
    } else if (strcmp(cmd, "D2") == 0) {
        led2_state = 0;
        leds_apply();
        ble_terminal_send("led2 desligado\r\n");
    } else if (strcmp(cmd, "D3") == 0) {
        led3_state = 0;
        leds_apply();
        ble_terminal_send("led3 desligado\r\n");
    } else if (strcmp(cmd, "T1") == 0) {
        led1_state ^= 1U;
        leds_apply();
        ble_terminal_send(led1_state ? "led1 ligado\r\n" : "led1 desligado\r\n");
    } else if (strcmp(cmd, "T2") == 0) {
        led2_state ^= 1U;
        leds_apply();
        ble_terminal_send(led2_state ? "led2 ligado\r\n" : "led2 desligado\r\n");
    } else if (strcmp(cmd, "T3") == 0) {
        led3_state ^= 1U;
        leds_apply();
        ble_terminal_send(led3_state ? "led3 ligado\r\n" : "led3 desligado\r\n");
    } else if (strcmp(cmd, "ON") == 0) {
        led1_state = 1;
        led2_state = 1;
        led3_state = 1;
        leds_apply();
        ble_terminal_send("todos ligados\r\n");
    } else if (strcmp(cmd, "OFF") == 0) {
        led1_state = 0;
        led2_state = 0;
        led3_state = 0;
        leds_apply();
        ble_terminal_send("todos desligados\r\n");
    } else if (strcmp(cmd, "STATUS") == 0) {
        ble_terminal_send_status();
    } else if ((strcmp(cmd, "HELP") == 0) || (strcmp(cmd, "?") == 0)) {
        ble_terminal_send("cmd: L1 L2 L3 D1 D2 D3 T1 T2 T3 ON OFF STATUS\r\n");
    } else {
        ble_terminal_send("comando invalido\r\n");
    }
}

/* ===================== Advertising ===================== */
/*
 * Inicia o advertising BLE usando os dados de anúncio e scan response.
 *
 * O advertising é o mecanismo que torna o dispositivo visível para o celular.
 * Aqui são anunciados:
 * - flags BLE padrão;
 * - nome completo do dispositivo: GD32-BLE-LED.
 */
static ble_status_t app_adv_start(void)
{
    ble_data_t adv_data = {0};
    ble_data_t adv_scanrsp_data = {0};
    ble_adv_data_set_t adv = {0};
    ble_adv_data_set_t scan_rsp = {0};
    uint8_t data[BLE_GAP_LEGACY_ADV_MAX_LEN] = {0};
    uint8_t idx = 0;

    /* Campo AD: tamanho = 2, tipo = flags, valor = BLE geral descobrível e sem BR/EDR. */
    data[idx++] = 2;
    data[idx++] = BLE_AD_TYPE_FLAGS;
    data[idx++] = BLE_GAP_ADV_FLAG_BR_EDR_NOT_SUPPORTED | BLE_GAP_ADV_FLAG_LE_GENERAL_DISC_MODE;

    /* Campo AD: nome completo do dispositivo. O tipo 0x09 significa Complete Local Name. */
    data[idx++] = DEV_NAME_LEN + 1;
    data[idx++] = 0x09;
    memcpy(&data[idx], dev_name, DEV_NAME_LEN);
    idx += DEV_NAME_LEN;

    /* Dados principais de advertising. */
    adv_data.len = idx;
    adv_data.p_data = data;

    /* Dados de scan response. Aqui reutiliza a parte do buffer que contém o nome. */
    adv_scanrsp_data.len = idx - 3;
    adv_scanrsp_data.p_data = &data[3];

    /* Força a pilha BLE a usar os dados definidos pela aplicação. */
    adv.data_force = true;
    adv.data.p_data_force = &adv_data;

    scan_rsp.data_force = true;
    scan_rsp.data.p_data_force = &adv_scanrsp_data;

    /* Inicia o advertising usando o índice criado anteriormente em app_adv_create(). */
    return ble_adv_start(app_adv_env.adv_idx, &adv, &scan_rsp, NULL);
}

/*
 * Callback de eventos do gerenciador de advertising.
 *
 * Quando a pilha BLE termina de criar o advertising, este callback recebe
 * BLE_ADV_STATE_CREATE. Nesse momento o código salva o adv_idx e inicia o advertising.
 */
static void app_adv_mgr_evt_hdlr(ble_adv_evt_t adv_evt, void *p_data, void *p_context)
{
    (void)p_context;

    if (adv_evt == BLE_ADV_EVT_STATE_CHG) {
        ble_adv_state_chg_t *p_chg = (ble_adv_state_chg_t *)p_data;
        ble_adv_state_t old_state = app_adv_env.adv_state;

        app_print("%s state change 0x%x ==> 0x%x, reason 0x%x\r\n", __func__,
                  old_state, p_chg->state, p_chg->reason);

        app_adv_env.adv_state = p_chg->state;

        if ((p_chg->state == BLE_ADV_STATE_CREATE) && (old_state == BLE_ADV_STATE_CREATING)) {
            app_adv_env.adv_idx = p_chg->adv_idx;
            (void)app_adv_start();
        }
    }
}

/*
 * Cria a instância de advertising BLE.
 *
 * Parâmetros principais:
 * - modo geral descobrível;
 * - endereço local estático;
 * - advertising legado;
 * - advertising conectável não direcionado;
 * - canais 37, 38 e 39 habilitados por ch_map = 0x07;
 * - PHY primário de 1 Mbps;
 * - intervalo de advertising configurado em 160 unidades BLE;
 * - reinício automático do advertising após desconexão.
 */
static ble_status_t app_adv_create(void)
{
    ble_adv_param_t adv_param = {0};

    app_adv_env.adv_state = BLE_ADV_STATE_CREATING;

    adv_param.param.disc_mode = BLE_GAP_ADV_MODE_GEN_DISC;
    adv_param.param.own_addr_type = BLE_GAP_LOCAL_ADDR_STATIC;
    adv_param.param.type = BLE_GAP_ADV_TYPE_LEGACY;
    adv_param.param.prop = BLE_GAP_ADV_PROP_UNDIR_CONN;
    adv_param.param.filter_pol = BLE_GAP_ADV_ALLOW_SCAN_ANY_CON_ANY;
    adv_param.param.ch_map = 0x07;
    adv_param.param.primary_phy = BLE_GAP_PHY_1MBPS;
    adv_param.param.adv_intv_min = 160;
    adv_param.param.adv_intv_max = 160;
    adv_param.restart_after_disconn = true;

    return ble_adv_create(&adv_param, app_adv_mgr_evt_hdlr, NULL);
}

/*
 * Função chamada quando a camada BLE está pronta para iniciar os recursos da aplicação.
 * Neste exemplo, o próximo passo é criar o advertising.
 */
void ble_task_ready(void)
{
    (void)app_adv_create();
}

/* ===================== Adapter / connection / security callbacks ===================== */
/*
 * Callback de eventos do adaptador BLE.
 *
 * O evento mais importante aqui é BLE_ADP_EVT_ENABLE_CMPL_INFO, que indica
 * que o adaptador BLE terminou de habilitar. Se deu certo, o código inicia o fluxo
 * de advertising chamando ble_task_ready().
 */
static void app_adp_evt_handler(ble_adp_evt_t event, ble_adp_data_u *p_data)
{
    if (event == BLE_ADP_EVT_ENABLE_CMPL_INFO) {
        if (p_data->adapter_info.status == BLE_ERR_NO_ERROR) {
            app_print("=== BLE adapter enable success ===\r\n");
            ble_task_ready();
        } else {
            app_print("=== BLE adapter enable fail: 0x%x ===\r\n", p_data->adapter_info.status);
        }
    }
}

/* Registra o callback de eventos do adaptador BLE. */
void app_adapter_init(void)
{
    ble_adp_callback_register(app_adp_evt_handler);
}

/*
 * Solicita o início do procedimento de segurança BLE para uma conexão.
 * A configuração usada não exige MITM nem bonding.
 */
void app_sec_send_security_req(uint8_t conidx)
{
    uint8_t auth = BLE_GAP_AUTH_REQ_NO_MITM_NO_BOND;

    if (ble_sec_security_req(conidx, auth) != BLE_ERR_NO_ERROR) {
        app_print("app_sec_send_security_req fail! \r\n");
    }
}

/*
 * Callback de eventos de conexão BLE.
 *
 * Trata conexão, desconexão, atualização de parâmetros, leitura do nome,
 * leitura da aparência e parâmetros preferidos de conexão do periférico.
 */
static void app_conn_evt_handler(ble_conn_evt_t event, ble_conn_data_u *p_data)
{
    switch (event) {
    case BLE_CONN_EVT_STATE_CHG:
        if (p_data->conn_state.state == BLE_CONN_STATE_DISCONNECTD) {
            /* Ao desconectar, invalida o índice da conexão e limpa a flag de conexão. */
            app_print("disconnected. conn idx: %u, conn_hdl: 0x%x reason 0x%x\r\n",
                      p_data->conn_state.info.discon_info.conn_idx,
                      p_data->conn_state.info.discon_info.conn_hdl,
                      p_data->conn_state.info.discon_info.reason);
            conn_idx = 0xFF;
            ble_connected = 0;
        } else if (p_data->conn_state.state == BLE_CONN_STATE_CONNECTED) {
            /* Ao conectar, salva o índice da conexão para poder enviar respostas depois. */
            app_print("connect success. conn idx:%u, conn_hdl:0x%x \r\n",
                      p_data->conn_state.info.conn_info.conn_idx,
                      p_data->conn_state.info.conn_info.conn_hdl);

            conn_idx = p_data->conn_state.info.conn_info.conn_idx;
            ble_connected = 1;

            /* Como periférico/slave, envia solicitação de segurança para a conexão. */
            if (p_data->conn_state.info.conn_info.role == BLE_SLAVE) {
                app_sec_send_security_req(conn_idx);
            }
        }
        break;

    case BLE_CONN_EVT_PARAM_UPDATE_IND:
        /*
         * O central solicitou atualização dos parâmetros de conexão.
         * O código imprime os valores recebidos e confirma a atualização.
         */
        app_print("conn idx %u, intv_min 0x%x, intv_max 0x%x, latency %u, supv_tout %u\r\n",
                  p_data->conn_param_req_ind.conn_idx, p_data->conn_param_req_ind.intv_min,
                  p_data->conn_param_req_ind.intv_max, p_data->conn_param_req_ind.latency,
                  p_data->conn_param_req_ind.supv_tout);
        ble_conn_param_update_cfm(p_data->conn_param_req_ind.conn_idx, true, 2, 4);
        break;

    case BLE_CONN_EVT_NAME_GET_IND:
        /* Responde à pilha BLE com o nome local do dispositivo. */
        ble_conn_name_get_cfm(p_data->name_get_ind.conn_idx, 0, p_data->name_get_ind.token,
                              DEV_NAME_LEN, (uint8_t *)dev_name, DEV_NAME_LEN);
        break;

    case BLE_CONN_EVT_APPEARANCE_GET_IND:
        /* Responde com appearance igual a 0, ou seja, sem categoria visual específica. */
        ble_conn_appearance_get_cfm(p_data->appearance_get_ind.conn_idx, 0,
                                    p_data->appearance_get_ind.token, 0);
        break;

    case BLE_CONN_EVT_SLAVE_PREFER_PARAM_GET_IND: {
        /* Parâmetros preferidos de conexão informados pelo periférico. */
        ble_gap_slave_prefer_param_t param;

        param.conn_intv_min = 8;
        param.conn_intv_max = 10;
        param.latency = 0;
        param.conn_tout = 200;

        ble_conn_slave_prefer_param_get_cfm(p_data->slave_prefer_param_get_ind.conn_idx, 0,
                                            p_data->slave_prefer_param_get_ind.token, &param);
    }
    break;

    default:
        /* Eventos não tratados explicitamente são ignorados. */
        break;
    }
}

/* Registra o callback de eventos de conexão BLE. */
void app_conn_mgr_init(void)
{
    ble_conn_callback_register(app_conn_evt_handler);
}

/*
 * Trata uma requisição de pareamento BLE.
 *
 * A configuração usada indica:
 * - sem autenticação especial;
 * - sem entrada/saída de usuário, isto é, sem display/teclado para PIN;
 * - chave de até 16 bytes;
 * - distribuição de chaves de identidade, assinatura e criptografia.
 */
static void app_pairing_req_hdlr(ble_gap_pairing_req_ind_t *p_ind)
{
    ble_gap_pairing_param_t param = {0};

    param.auth = BLE_GAP_AUTH_MASK_NONE;
    param.iocap = BLE_GAP_IO_CAP_NO_IO;
    param.key_size = 16;
    param.ikey_dist = BLE_GAP_KDIST_IDKEY | BLE_GAP_KDIST_SIGNKEY | BLE_GAP_KDIST_ENCKEY;
    param.rkey_dist = BLE_GAP_KDIST_IDKEY | BLE_GAP_KDIST_SIGNKEY | BLE_GAP_KDIST_ENCKEY;

    ble_sec_pairing_req_cfm(p_ind->conn_idx, true, &param, BLE_GAP_NO_SEC);
}

/* Callback de eventos de segurança BLE. */
static void app_sec_evt_handler(ble_sec_evt_t event, ble_sec_data_u *p_data)
{
    switch (event) {
    case BLE_SEC_EVT_PAIRING_REQ_IND:
        app_pairing_req_hdlr((ble_gap_pairing_req_ind_t *)p_data);
        break;

    case BLE_SEC_EVT_PAIRING_SUCCESS_INFO:
        app_print("pairing success\r\n");
        break;

    case BLE_SEC_EVT_PAIRING_FAIL_INFO:
        app_print("pairing fail\r\n");
        break;

    default:
        break;
    }
}

/* Registra o callback de eventos de segurança BLE. */
void app_sec_mgr_init(void)
{
    ble_sec_callback_register(app_sec_evt_handler);
}

/* ===================== Datatrans RX callback ===================== */
/*
 * Callback chamado quando o celular escreve dados no serviço BLE datatrans.
 *
 * Este é o ponto em que o comando enviado pelo aplicativo Android entra na aplicação.
 * A função copia os bytes recebidos para um buffer local terminado em '\0' e chama
 * process_ble_command() para interpretar o comando.
 */
void app_datatrans_srv_rx_callback(uint8_t rx_conn_idx, uint16_t data_len, uint8_t *p_data)
{
    char cmd[32];
    uint16_t copy_len;

    /* Ignora pacotes vazios ou ponteiro inválido. */
    if ((p_data == NULL) || (data_len == 0U)) {
        return;
    }

    /* Atualiza a conexão ativa usando o índice recebido no callback. */
    conn_idx = rx_conn_idx;
    ble_connected = 1;

    /* Limita a cópia para evitar estouro do buffer cmd. */
    copy_len = data_len;
    if (copy_len >= sizeof(cmd)) {
        copy_len = sizeof(cmd) - 1U;
    }

    /* Copia os dados recebidos e força terminação nula para tratar como string C. */
    memcpy(cmd, p_data, copy_len);
    cmd[copy_len] = '\0';

    process_ble_command(cmd);
}

/* ===================== BLE initialization ===================== */
/*
 * Inicializa a pilha BLE, registra callbacks e habilita interrupções BLE.
 *
 * Esta função configura:
 * - alimentação do bloco BLE;
 * - papel GAP como periférico;
 * - modo de pareamento;
 * - privacidade;
 * - prioridades e stacks das tarefas BLE;
 * - permissões de nome e appearance;
 * - interface de sistema operacional usada pela pilha BLE;
 * - serviço datatrans e callback de recepção.
 */
void ble_init(void)
{
    ble_init_param_t param = {0};

    /*
     * Tabela de funções do sistema operacional/wrapper usadas internamente pela pilha BLE.
     * A pilha BLE chama essas funções para alocar memória, criar tarefas, usar filas,
     * dormir por milissegundos e obter bytes aleatórios.
     */
    ble_os_api_t os_interface = {
        .os_malloc = sys_malloc,
        .os_calloc = sys_calloc,
        .os_mfree = sys_mfree,
        .os_memset = sys_memset,
        .os_memcpy = sys_memcpy,
        .os_memcmp = sys_memcmp,
        .os_task_create = sys_task_create,
        .os_task_init_notification = sys_task_init_notification,
        .os_task_wait_notification = sys_task_wait_notification,
        .os_task_notify = sys_task_notify,
        .os_task_delete = sys_task_delete,
        .os_ms_sleep = sys_ms_sleep,
        .os_current_task_handle_get = sys_current_task_handle_get,
        .os_queue_init = sys_queue_init,
        .os_queue_free = sys_queue_free,
        .os_queue_write = sys_queue_write,
        .os_queue_read = sys_queue_read,
        .os_random_bytes_get = sys_random_bytes_get,
    };

    /* Liga/alimenta o bloco BLE antes de inicializar o software BLE. */
    ble_power_on();

    /* Configuração principal da pilha BLE. */
    param.role = BLE_GAP_ROLE_PERIPHERAL;
    param.keys_user_mgr = false;
    param.pairing_mode = BLE_GAP_PAIRING_SECURE_CONNECTION | BLE_GAP_PAIRING_LEGACY;
    param.privacy_cfg = BLE_GAP_PRIV_CFG_PRIV_EN_BIT;
    param.ble_task_stack_size = BLE_STACK_TASK_STACK_SIZE;
    param.ble_task_priority = BLE_STACK_TASK_PRIORITY;
    param.ble_app_task_stack_size = BLE_APP_TASK_STACK_SIZE;
    param.ble_app_task_priority = BLE_APP_TASK_PRIORITY;
    param.name_perm = BLE_GAP_WRITE_NOT_ENC;
    param.appearance_perm = BLE_GAP_WRITE_NOT_ENC;
    param.en_cfg = 0;
    param.p_os_api = &os_interface;

    /* Inicializa a pilha BLE de software com os parâmetros definidos. */
    ble_sw_init(&param);

    /* Registra callbacks dos módulos BLE usados pela aplicação. */
    app_adapter_init();
    app_conn_mgr_init();
    app_sec_mgr_init();

    /* Inicializa o serviço BLE datatrans e registra o callback de recepção de dados. */
    ble_datatrans_srv_init();
    ble_datatrans_srv_rx_cb_reg(app_datatrans_srv_rx_callback);

    /* The BLE interrupt must be enabled after ble_sw_init. */
    /* Habilita a interrupção BLE somente depois da inicialização da pilha. */
    ble_irq_enable();
}

/*
 * Função principal da aplicação.
 *
 * Fluxo geral:
 * 1. Inicializa o sistema operacional/wrapper;
 * 2. Inicializa a plataforma/hardware base;
 * 3. Inicializa os GPIOs dos LEDs;
 * 4. Imprime mensagens de diagnóstico;
 * 5. Inicializa a pilha BLE e o serviço datatrans;
 * 6. Inicia o scheduler/RTOS;
 * 7. Mantém um loop infinito de segurança caso o scheduler retorne.
 */
int main(void)
{
    sys_os_init();
    platform_init();

    leds_init();
    app_print("BLE LED terminal example\r\n");
    app_print("Device name: %s\r\n", dev_name);
    app_print("LED1=PB0, LED2=PA12, LED3=PB4\r\n");

    ble_init();
    sys_os_start();

    for (;;) {
    }
}
