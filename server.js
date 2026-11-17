const express = require('express');
const cors = require('cors');
const path = require('path');

const app = express();
app.use(cors());
app.use(express.json());
app.use(express.static(path.join(__dirname, 'public')));

// Estado Global do Laboratório
let modoGlobal = 'ATIVA'; // 'ATIVA' ou 'ENCERRADA'

// Banco de dados em memória para os Grupos 1 a 7
const grupos = {
  1: { nome: 'Ar-Condicionado (G1)', dados: {}, ultimoContato: 0, comando: 'INICIAR' },
  2: { nome: 'Iluminação (G2)', dados: {}, ultimoContato: 0, comando: 'INICIAR' },
  3: { nome: 'Controle de Porta (G3)', dados: {}, ultimoContato: 0, comando: 'INICIAR' },
  4: { nome: 'Réguas AC Notebooks (G4)', dados: {}, ultimoContato: 0, comando: 'INICIAR' },
  5: { nome: 'Monitoramento Geral (G5)', dados: {}, ultimoContato: 0, comando: 'INICIAR' },
  6: { nome: 'Exaustor / Ventilação (G6)', dados: {}, ultimoContato: 0, comando: 'INICIAR' },
  7: { nome: 'Console Mestre Prof. (G7)', dados: {}, ultimoContato: 0, comando: 'INICIAR' }
};

// Rota obrigatória: Recebe telemetria dos Grupos 1 a 7
app.post('/api/telemetria', (req, res) => {
  const payload = req.body;
  const id = Number(payload.grupo);

  if (grupos[id]) {
    grupos[id].dados = payload;
    grupos[id].ultimoContato = Date.now();

    // Se o botão físico do Grupo 7 foi pressionado, aplica a macro em todos os grupos
    if (id === 7 && payload.comandoFisico) {
      aplicarMacroGlobal(payload.modoAula);
    }
  }

  // Responde com o comando agendado para aquele grupo específico
  res.json({
    status: 'OK',
    modoGlobal: modoGlobal,
    comando: grupos[id] ? grupos[id].comando : 'INICIAR'
  });
});

// Rota para acionar a Macro pelo Dashboard Web
app.post('/api/macro', (req, res) => {
  const { acao } = req.body; // 'ATIVA' ou 'ENCERRADA'
  aplicarMacroGlobal(acao);
  res.json({ status: 'OK', modoGlobal });
});

function aplicarMacroGlobal(novoModo) {
  modoGlobal = novoModo;
  const cmd = novoModo === 'ATIVA' ? 'INICIAR' : 'PARAR';
  for (let i = 1; i <= 7; i++) {
    grupos[i].comando = cmd;
  }
  console.log(`[MACRO GLOBAL] Modo alterado para: ${modoGlobal} | Comando enviado: ${cmd}`);
}

// Rota obrigatória: Envia status consolidado para o Dashboard Geral
app.get('/api/status-geral', (req, res) => {
  const agora = Date.now();
  const statusGrupos = [];
  let onlineCount = 0;

  for (let i = 1; i <= 7; i++) {
    // Considera ONLINE se enviou dados nos últimos 10 segundos
    const online = (agora - grupos[i].ultimoContato) < 10000;
    if (online) onlineCount++;

    statusGrupos.push({
      id: i,
      nome: grupos[i].nome,
      online: online,
      comandoAtual: grupos[i].comando,
      dados: grupos[i].dados
    });
  }

  // Cálculo do Índice Global de Eficiência (0% a 100%)
  const eficiencia = Math.round((onlineCount / 7) * 100);

  res.json({
    modoGlobal,
    eficiencia,
    grupos: statusGrupos
  });
});

const PORT = 3000;
app.listen(PORT, () => {
  console.log(`Central Supervisória rodando em http://localhost:${PORT}`);
});
