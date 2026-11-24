const express = require('express');
const path = require('path');

const app = express();
const PORT = 3000
;
const MAX_HISTORICO = 40;
const MAX_LOGS = 60;
const TIMEOUT_OFFLINE_MS = 10000; // 10 segundos sem heartbeat = OFFLINE

app.use(express.json());

// Middleware CORS local (evita dependência externa extra)
app.use((req, res, next) => {
  res.header('Access-Control-Allow-Origin', '*');
  res.header('Access-Control-Allow-Methods', 'GET, POST, OPTIONS');
  res.header('Access-Control-Allow-Headers', 'Content-Type');
  if (req.method === 'OPTIONS') return res.sendStatus(200);
  next();
});

// Serve index.html e chart.umd.min.js localmente (Zero Internet)
app.use(express.static(path.join(__dirname, 'public')));

// Estado global do laboratório
let modoAula = 'ATIVA'; // 'ATIVA' | 'ENCERRADA'

// Estrutura inicial dos 7 grupos da somativa
const bancadas = {};
const comandosPendentes = {};
const ultimoLogAlerta = {};

for (let i = 1; i <= 7; i++) {
  const id = `grupo_0${i}`;
  bancadas[id] = {
    grupo_id: id,
    nome_subsistema: i === 7 ? 'Console_Mestre_Professor' : `Subsistema_Bancada_0${i}`,
    ip_local: '---',
    mac_address: '---',
    temperatura: 0,
    umidade: 0,
    variavel_principal_nome: i === 7 ? 'Luminosidade_Mesa_Pct' : 'Variavel_Principal',
    variavel_principal_valor: 0,
    rele_ativo: false,
    status_alerta: 'NORMAL',
    ultimo_contato_ms: 0,
    ultima_atualizacao: null
  };
  comandosPendentes[id] = 'NENHUM';
  ultimoLogAlerta[id] = 0;
}

const historicoTelemetria = [];
const logsEventos = [];

function registrarLog(nivel, origem, mensagem) {
  const agora = new Date();
  const hora = agora.toLocaleTimeString('pt-BR', { hour12: false });
  logsEventos.unshift({
    hora,
    nivel,   // 'INFO' | 'ATENCAO' | 'CRITICO' | 'MACRO'
    origem,
    mensagem
  });
  if (logsEventos.length > MAX_LOGS) {
    logsEventos.pop();
  }
  console.log(`[${hora}] [${nivel}] [${origem}] ${mensagem}`);
}

function normalizarGrupoId(payload) {
  if (payload.grupo_id) {
    return String(payload.grupo_id).trim().toLowerCase();
  }
  if (payload.grupo !== undefined) {
    const num = Number(payload.grupo);
    if (!Number.isNaN(num) && num >= 1 && num <= 9) {
      return `grupo_0${num}`;
    }
  }
  return null;
}

function normalizarStatusAlerta(status) {
  if (!status) return 'NORMAL';
  const s = String(status)
    .normalize('NFD')
    .replace(/[\u0300-\u036f]/g, '')
    .toUpperCase()
    .trim();
  if (s === 'CRITICO' || s === 'ATENCAO' || s === 'NORMAL') return s;
  return 'NORMAL';
}

// ============================================================================
// POST /api/telemetria - Recebe dados dos ESP32 (Grupos 01 a 07)
// ============================================================================
app.post('/api/telemetria', (req, res) => {
  const payload = req.body || {};
  const id = normalizarGrupoId(payload);

  if (!id || !bancadas[id]) {
    return res.status(400).json({
      erro: 'grupo_id inválido ou ausente. Esperado: grupo_01 até grupo_07.'
    });
  }

  const agoraMs = Date.now();
  const estavaOffline = (agoraMs - bancadas[id].ultimo_contato_ms) > TIMEOUT_OFFLINE_MS;
  const statusAnterior = bancadas[id].status_alerta;

  const temp = Number(payload.temperatura ?? 0);
  const umid = Number(payload.umidade ?? 0);
  const varNome = payload.variavel_principal_nome || bancadas[id].variavel_principal_nome;
  const varValor = Number(payload.variavel_principal_valor ?? 0);
  const releAtivo = Boolean(payload.rele_ativo);
  const statusAlerta = normalizarStatusAlerta(payload.status_alerta);

  // Atualiza registro consolidado da bancada
  bancadas[id] = {
    grupo_id: id,
    nome_subsistema: payload.nome_subsistema || bancadas[id].nome_subsistema,
    ip_local: payload.ip_local || req.socket.remoteAddress || '---',
    mac_address: payload.mac_address || bancadas[id].mac_address,
    temperatura: Number.isFinite(temp) ? Number(temp.toFixed(1)) : 0,
    umidade: Number.isFinite(umid) ? Number(umid.toFixed(1)) : 0,
    variavel_principal_nome: varNome,
    variavel_principal_valor: Number.isFinite(varValor) ? Number(varValor.toFixed(1)) : 0,
    rele_ativo: releAtivo,
    status_alerta: statusAlerta,
    ultimo_contato_ms: agoraMs,
    ultima_atualizacao: new Date(agoraMs).toLocaleTimeString('pt-BR', { hour12: false })
  };

  if (estavaOffline) {
    registrarLog('INFO', id.toUpperCase(), `Subsistema "${bancadas[id].nome_subsistema}" conectado (${bancadas[id].ip_local}).`);
  }

  // Trata acionamento físico da Chave Geral no Console Mestre (Grupo 07)
  if (id === 'grupo_07' && payload.comando_fisico === true) {
    modoAula = payload.modo_aula === 'ENCERRADA' ? 'ENCERRADA' : 'ATIVA';
    const cmdGlobal = modoAula === 'ATIVA' ? 'LIGAR' : 'PARAR';

    Object.keys(comandosPendentes).forEach((gId) => {
      if (gId !== 'grupo_07') {
        comandosPendentes[gId] = cmdGlobal;
      }
    });

    registrarLog(
      'MACRO',
      'GRUPO_07 (BOTÃO)',
      `Chave física acionada: Modo Aula -> ${modoAula}. Propagando "${cmdGlobal}" para os grupos 01..06.`
    );
  }

  // Registra alertas críticos ou de atenção sem inundar o log
  if (statusAlerta !== 'NORMAL' && (statusAlerta !== statusAnterior || (agoraMs - ultimoLogAlerta[id]) > 15000)) {
    ultimoLogAlerta[id] = agoraMs;
    registrarLog(
      statusAlerta,
      id.toUpperCase(),
      `Alerta ${statusAlerta}: Temp=${bancadas[id].temperatura}°C | Umid=${bancadas[id].umidade}% | ${varNome}=${bancadas[id].variavel_principal_valor}`
    );
  }

  // Alimenta histórico para os gráficos em tempo real
  historicoTelemetria.push({
    hora: bancadas[id].ultima_atualizacao,
    grupo_id: id,
    temperatura: bancadas[id].temperatura,
    umidade: bancadas[id].umidade,
    variavel_valor: bancadas[id].variavel_principal_valor
  });
  if (historicoTelemetria.length > MAX_HISTORICO) {
    historicoTelemetria.shift();
  }

  // Consome comando pendente para este grupo (LIGAR, PARAR ou NENHUM)
  const comandoResposta = comandosPendentes[id] || 'NENHUM';
  comandosPendentes[id] = 'NENHUM';

  if (comandoResposta !== 'NENHUM') {
    registrarLog('INFO', 'SERVIDOR -> ' + id.toUpperCase(), `Comando "${comandoResposta}" entregue ao ESP32.`);
  }

  return res.status(201).json({
    status: 'ok',
    grupo_id: id,
    comando: comandoResposta,
    modo_aula: modoAula,
    timestamp: new Date(agoraMs).toISOString()
  });
});

// ============================================================================
// POST /api/comando - Comando Individual por Card (grupo específico)
// ============================================================================
app.post('/api/comando', (req, res) => {
  const { comando, alvo } = req.body || {};
  const cmd = String(comando || '').toUpperCase().trim();

  if (cmd !== 'LIGAR' && cmd !== 'PARAR') {
    return res.status(400).json({ erro: 'Comando inválido. Use LIGAR ou PARAR.' });
  }

  const grupoAlvo = String(alvo || '').toLowerCase().trim();
  if (!bancadas[grupoAlvo]) {
    return res.status(404).json({ erro: 'Grupo alvo não encontrado.' });
  }

  comandosPendentes[grupoAlvo] = cmd;
  registrarLog('INFO', 'DASHBOARD', `Comando individual "${cmd}" enfileirado para ${grupoAlvo.toUpperCase()}.`);
  return res.json({ status: 'ok', comando: cmd, alvo: grupoAlvo });
});

// ============================================================================
// POST /api/comando-geral - Macro Global via botão do Dashboard (LIGAR/PARAR TUDO)
// ============================================================================
app.post('/api/comando-geral', (req, res) => {
  const { comando } = req.body || {};
  const cmd = String(comando || '').toUpperCase().trim();

  if (cmd !== 'LIGAR' && cmd !== 'PARAR') {
    return res.status(400).json({ erro: 'Comando inválido. Use LIGAR ou PARAR.' });
  }

  modoAula = cmd === 'LIGAR' ? 'ATIVA' : 'ENCERRADA';
  Object.keys(comandosPendentes).forEach((gId) => {
    comandosPendentes[gId] = cmd;
  });

  registrarLog('MACRO', 'DASHBOARD', `Macro Global "${cmd}" acionada para todas as 7 bancadas. Modo: ${modoAula}`);
  return res.json({ status: 'ok', modo_aula: modoAula, comando: cmd, alvo: 'TODOS' });
});

// ============================================================================
// GET /api/status-geral - Fornece estado consolidado para o Dashboard
// ============================================================================
app.get('/api/status-geral', (req, res) => {
  const agoraMs = Date.now();
  let onlineCount = 0;

  const gruposFormatados = {};
  Object.entries(bancadas).forEach(([id, b]) => {
    const online = b.ultimo_contato_ms > 0 && (agoraMs - b.ultimo_contato_ms) <= TIMEOUT_OFFLINE_MS;
    if (online) onlineCount++;
    gruposFormatados[id] = {
      ...b,
      nome: b.nome_subsistema.replace(/_/g, ' '),
      online
    };
  });

  const eficiencia_global = Math.round((onlineCount / 7) * 100);

  res.json({
    modo_aula: modoAula,
    eficiencia_global,
    bancadas_online: onlineCount,
    servidor_hora: new Date(agoraMs).toLocaleTimeString('pt-BR', { hour12: false }),
    grupos: gruposFormatados,
    historico: historicoTelemetria,
    logs: logsEventos
  });
});

// ============================================================================
// GET /api/dados - Alias mantido para compatibilidade
// ============================================================================
app.get('/api/dados', (req, res) => {
  const agoraMs = Date.now();
  const listaGrupos = Object.values(bancadas).map((b) => ({
    ...b,
    online: b.ultimo_contato_ms > 0 && (agoraMs - b.ultimo_contato_ms) <= TIMEOUT_OFFLINE_MS
  }));

  res.json({
    modo_aula: modoAula,
    servidor_hora: new Date(agoraMs).toLocaleTimeString('pt-BR', { hour12: false }),
    grupos: listaGrupos,
    historico: historicoTelemetria,
    logs: logsEventos
  });
});

registrarLog('INFO', 'SISTEMA', 'Servidor Console Mestre (Grupo 07) iniciado na LAN.');

app.listen(PORT, '0.0.0.0', () => {
  console.log(`Console Mestre rodando em http://0.0.0.0:${PORT} (acessível na LAN inteira)`);
});