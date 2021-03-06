
#include <ptlib.h>

#include "mcu.h"

////////////////////////////////////////////////////////////////////////////////////////////////////

PString RegistrarAccount::GetUrl()
{
  PString url;
  if(account_type == ACCOUNT_TYPE_SIP)
  {
    url = "sip:"+username+"@"+host;
    if(port != 0 && port != 5060)
      url += ":"+PString(port);
    if(transport != "" && transport != "*")
      url += ";transport="+transport;
  }
  else if(account_type == ACCOUNT_TYPE_H323)
  {
    url = "h323:"+username+"@"+host;
    if(port != 0 && port != 1720)
      url += ":"+PString(port);
  }
  else if(account_type == ACCOUNT_TYPE_RTSP)
  {
    if(username.Left(7) != "rtsp://")
      url += "rtsp://";
    url += username;
  }
  return url;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

PString RegistrarAccount::GetAuthStr()
{
  if(nonce == "")
    nonce = PGloballyUniqueID().AsString();
  PString sip_auth_str = scheme+" "+"realm=\""+domain+"\",nonce=\""+nonce+"\",algorithm="+algorithm;
  return sip_auth_str;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void RegistrarAccount::Unlock()
{
  registrar->GetAccountList().Release(id);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void RegistrarConnection::Unlock()
{
  registrar->GetConnectionList().Release(id);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void RegistrarSubscription::Unlock()
{
  registrar->GetSubscriptionList().Release(id);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

RegistrarAccount * Registrar::InsertAccountWithLock(RegAccountTypes account_type, const PString & username)
{
  long id = accountList.GetNextID();
  RegistrarAccount *raccount = new RegistrarAccount(this, id, account_type, username);
  PString name = PString(raccount->account_type)+":"+raccount->username;
  accountList.Insert(id, raccount, name);
  return raccount;
}

RegistrarAccount * Registrar::FindAccountWithLock(RegAccountTypes account_type, const PString & username)
{
  for(MCURegistrarAccountList::shared_iterator it = accountList.begin(); it != accountList.end(); ++it)
  {
    if(it->username == username && (account_type == ACCOUNT_TYPE_UNKNOWN || account_type == it->account_type))
    {
      return it.GetCapturedObject();
    }
  }
  return NULL;
}

PString Registrar::FindAccountNameFromH323Id(const PString & h323id)
{
  for(MCURegistrarAccountList::shared_iterator it = accountList.begin(); it != accountList.end(); ++it)
  {
    if(it->h323id == h323id)
      return it->username;
  }
  return "";
}

////////////////////////////////////////////////////////////////////////////////////////////////////

RegistrarSubscription * Registrar::InsertSubWithLock(const PString & username_in, const PString & username_out)
{
  long id = accountList.GetNextID();
  RegistrarSubscription *rsub = new RegistrarSubscription(this, id, username_in, username_out);
  subscriptionList.Insert(id, rsub, rsub->username_pair);
  return rsub;
}

RegistrarSubscription *Registrar::FindSubWithLock(const PString & username_pair)
{
  MCURegistrarSubscriptionList::shared_iterator it = subscriptionList.Find(username_pair);
  if(it != subscriptionList.end())
  {
    return it.GetCapturedObject();
  }
  return NULL;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

RegistrarConnection * Registrar::InsertRegConnWithLock(const PString & callToken, const PString & username_in, const PString & username_out)
{
  long id = connectionList.GetNextID();
  RegistrarConnection *rconn = new RegistrarConnection(this, id, callToken, username_in, username_out);
  connectionList.Insert(id, rconn, callToken);
  return rconn;
}

RegistrarConnection * Registrar::FindRegConnWithLock(const PString & callToken)
{
  for(MCURegistrarConnectionList::shared_iterator it = connectionList.begin(); it != connectionList.end(); ++it)
  {
    if(it->callToken_in == callToken || it->callToken_out == callToken)
    {
      return it.GetCapturedObject();
    }
  }
  return NULL;
}

RegistrarConnection * Registrar::FindRegConnUsernameWithLock(const PString & username)
{
  for(MCURegistrarConnectionList::shared_iterator it = connectionList.begin(); it != connectionList.end(); ++it)
  {
    if(it->username_in == username || it->username_out == username)
    {
      return it.GetCapturedObject();
    }
  }
  return NULL;
}

bool Registrar::HasRegConn(const PString & callToken)
{
  MCURegistrarConnectionList::shared_iterator it = connectionList.Find(callToken);
  if(it != connectionList.end())
    return true;
  return false;
}

bool Registrar::HasRegConnUsername(const PString & username)
{
  for(MCURegistrarConnectionList::shared_iterator it = connectionList.begin(); it != connectionList.end(); ++it)
  {
    if(it->username_in == username || it->username_out == username)
      return true;
  }
  return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void Registrar::ConnectionCreated(const PString & callToken)
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void Registrar::ConnectionEstablished(const PString & callToken)
{
  PString *cmd = new PString("established:"+callToken);
  regQueue.Push(cmd);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void Registrar::ConnectionCleared(const PString & callToken)
{
  PString *cmd = new PString("cleared:"+callToken);
  regQueue.Push(cmd);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void Registrar::SetRequestedRoom(const PString & callToken, PString & requestedRoom)
{
  RegistrarConnection *rconn = FindRegConnWithLock(callToken);
  if(rconn)
  {
    if(rconn->roomname != "")
      requestedRoom = rconn->roomname;
    rconn->Unlock();
  }
}

////////////////////////////////////////////////////////////////////////////////////////////////////

BOOL Registrar::MakeCall(const PString & room, const PString & to, PString & callToken)
{
  // the default protocol H.323
  // correct formats - proto:username, proto:username@, proto:ip, proto:@ip
  // wrong formats - proto:@username, proto:ip@

  PWaitAndSignal m(mutex);

  PString address = to;
  MCUURL url(address);

  RegAccountTypes account_type = ACCOUNT_TYPE_UNKNOWN;
  if(url.GetScheme() == "sip")
    account_type = ACCOUNT_TYPE_SIP;
  else if(url.GetScheme() == "h323")
    account_type = ACCOUNT_TYPE_H323;
  else if(url.GetScheme() == "rtsp")
    account_type = ACCOUNT_TYPE_RTSP;
  else
    return FALSE;

  PString username_out;
  if(url.GetUserName() != "")
    username_out = url.GetUserName();
  else if(url.GetHostName() != "")
    username_out = url.GetHostName();
  else
    return FALSE;

  RegistrarAccount *raccount_out = NULL;
  RegistrarConnection *rconn = NULL;

  raccount_out = FindAccountWithLock(account_type, username_out);
  if(raccount_out)
  {
    // update address from account
    address = raccount_out->GetUrl();
    raccount_out->Unlock();
    // update url
    url = MCUURL(address);
  }
  // initial username_out, can be empty
  username_out = url.GetUserName();

  if(account_type == ACCOUNT_TYPE_SIP)
  {
    callToken = PGloballyUniqueID().AsString();
    PString *cmd = new PString("invite:"+room+","+address+","+callToken);
    sep->GetQueue().Push(cmd);
  }
  else if(account_type == ACCOUNT_TYPE_H323)
  {
    callToken = PGloballyUniqueID().AsString();
    PString *cmd = new PString("invite:"+room+","+address+","+callToken);
    regQueue.Push(cmd);
  }
  else if(account_type == ACCOUNT_TYPE_RTSP)
  {
    PString callToken = PGloballyUniqueID().AsString();
    MCURtspConnection *rCon = new MCURtspConnection(sep, ep, callToken);
    if(rCon->Connect(room, address) == FALSE)
    {
      rCon->LeaveMCU();
      return FALSE;
    }
  }

  if(callToken != "")
  {
    rconn = InsertRegConnWithLock(callToken, room, username_out);
    rconn->account_type_out = account_type;
    rconn->callToken_out = callToken;
    rconn->roomname = room;
    rconn->state = CONN_MCU_WAIT;
    rconn->Unlock();
  }

  if(callToken == "")
    return FALSE;
  return TRUE;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

BOOL Registrar::InternalMakeCall(RegistrarConnection *rconn, const PString & username_in, const PString & username_out)
{
  RegistrarAccount *raccount_in = FindAccountWithLock(ACCOUNT_TYPE_UNKNOWN, username_in);
  RegistrarAccount *raccount_out = FindAccountWithLock(ACCOUNT_TYPE_UNKNOWN, username_out);
  BOOL ret = InternalMakeCall(rconn, raccount_in, raccount_out);

  if(raccount_in) raccount_in->Unlock();
  if(raccount_out) raccount_out->Unlock();
  return ret;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

BOOL Registrar::InternalMakeCall(RegistrarConnection *rconn, RegistrarAccount *raccount_in, RegistrarAccount *raccount_out)
{
  if(!rconn || !raccount_in || !raccount_out)
    return FALSE;

  rconn->account_type_in = raccount_in->account_type;
  rconn->account_type_out = raccount_out->account_type;

  PString address = raccount_out->GetUrl();

  if(raccount_out->account_type == ACCOUNT_TYPE_SIP)
  {
    PString callToken = PGloballyUniqueID().AsString();
    PString *cmd = new PString("invite:"+rconn->username_in+","+address+","+callToken);
    sep->GetQueue().Push(cmd);
    rconn->callToken_out = callToken;
    return TRUE;
  }
  else if(raccount_out->account_type == ACCOUNT_TYPE_H323)
  {
    PString callToken_out;
    void *userData = new PString(rconn->username_in);
    ep->MakeCall(address, callToken_out, userData);
    if(callToken_out != "")
    {
      rconn->callToken_out = callToken_out;
      return TRUE;
    }
  }
  return FALSE;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void Registrar::IncomingCallAccept(RegistrarConnection *rconn)
{
  if(rconn->account_type_in == ACCOUNT_TYPE_SIP)
  {
    msg_t *msg = rconn->GetInviteMsgCopy();
    sep->CreateIncomingConnection(msg);
    msg_destroy(msg);
  }
  else if(rconn->account_type_in == ACCOUNT_TYPE_H323)
  {
    MCUH323Connection *conn = (MCUH323Connection *)ep->FindConnectionWithLock(rconn->callToken_in);
    if(conn)
    {
      conn->AnsweringCall(H323Connection::AnswerCallNow);
      conn->Unlock();
    }
  }
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void Registrar::IncomingCallCancel(RegistrarConnection *rconn)
{
  if(rconn->callToken_in != "")
    Leave(rconn->account_type_in, rconn->callToken_in);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void Registrar::OutgoingCallCancel(RegistrarConnection *rconn)
{
  if(rconn->callToken_out != "")
    Leave(rconn->account_type_out, rconn->callToken_out);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void Registrar::Leave(int account_type, const PString & callToken)
{
  MCUH323Connection *conn = NULL;
  if(account_type == ACCOUNT_TYPE_SIP)
    conn = (MCUSipConnection *)ep->FindConnectionWithLock(callToken);
  else if(account_type == ACCOUNT_TYPE_H323)
    conn = (MCUH323Connection *)ep->FindConnectionWithLock(callToken);
  if(conn)
  {
    conn->LeaveMCU();
    conn->Unlock();
  }
}

////////////////////////////////////////////////////////////////////////////////////////////////////

PStringArray Registrar::GetAccountStatusList()
{
  return account_status_list;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void Registrar::AccountThread(PThread &, INT)
{
  while(!terminating)
  {
    PThread::Sleep(100);
    if(terminating)
      break;

    PTime now;
    for(MCURegistrarAccountList::shared_iterator it = accountList.begin(); it != accountList.end(); ++it)
    {
      RegistrarAccount *raccount = *it;
      // expires
      if(raccount->registered)
      {
        if(now > raccount->start_time + PTimeInterval(raccount->expires*1000))
          raccount->registered = FALSE;
      }
    }
  }
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void Registrar::ConnectionThread(PThread &, INT)
{
  while(!terminating)
  {
    PThread::Sleep(100);
    if(terminating)
      break;

    PTime now;
    for(MCURegistrarConnectionList::shared_iterator it = connectionList.begin(); it != connectionList.end(); ++it)
    {
      RegistrarConnection *rconn = *it;
      // remove empty connection
      if(rconn->state == CONN_IDLE)
      {
        if(connectionList.Erase(it))
          delete rconn;
        continue;
      }
      // MCU call answer limit
      if(rconn->state == CONN_MCU_WAIT)
      {
        if(now > rconn->start_time + PTimeInterval(rconn->accept_timeout*1000))
        {
          OutgoingCallCancel(rconn);
          rconn->state = CONN_END;
        }
      }
      // internal call answer limit
      if(rconn->state == CONN_WAIT)
      {
        if(now > rconn->start_time + PTimeInterval(rconn->accept_timeout*1000))
        {
          IncomingCallCancel(rconn);
          OutgoingCallCancel(rconn);
          rconn->state = CONN_END;
        }
      }
      // make internal call
      if(rconn->state == CONN_WAIT)
      {
        if(rconn->callToken_out == "")
        {
          if(!InternalMakeCall(rconn, rconn->username_in, rconn->username_out))
          {
            rconn->state = CONN_CANCEL_IN;
          }
        }
      }
      // accept incoming
      if(rconn->state == CONN_ACCEPT_IN)
      {
        IncomingCallAccept(rconn);
        rconn->state = CONN_ESTABLISHED;
      }
      // cancel incoming
      if(rconn->state == CONN_CANCEL_IN)
      {
        IncomingCallCancel(rconn);
        rconn->state = CONN_END;
      }
      // cancel outgoing
      if(rconn->state == CONN_CANCEL_OUT)
      {
        OutgoingCallCancel(rconn);
        rconn->state = CONN_END;
      }
      // leave incoming
      if(rconn->state == CONN_LEAVE_IN)
      {
        Leave(rconn->account_type_in, rconn->callToken_in);
        rconn->state = CONN_END;
      }
      // leave outgoing
      if(rconn->state == CONN_LEAVE_OUT)
      {
        Leave(rconn->account_type_out, rconn->callToken_out);
        rconn->state = CONN_END;
      }
      // internal call end
      if(rconn->state == CONN_END)
      {
        rconn->state = CONN_IDLE;
      }
    }
  }
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void Registrar::BookThread(PThread &, INT)
{
  while(!terminating)
  {
    PThread::Sleep(1000);
    if(terminating)
      break;

    PStringArray list;
    for(MCURegistrarAccountList::shared_iterator it = accountList.begin(); it != accountList.end(); ++it)
    {
      RegistrarAccount *raccount = *it;

      int reg_state = 0;
      int conn_state = 0;
      int ping_state = 0;
      PString remote_application = raccount->remote_application;
      PString reg_info;
      PString conn_info;
      PString ping_info;

      if(raccount->registered)
        reg_state = 2;
      else if(raccount->enable)
        reg_state = 1;

      RegistrarConnection *rconn = FindRegConnUsernameWithLock(raccount->username);
      if(rconn)
      {
        if(rconn->state == CONN_WAIT || rconn->state == CONN_MCU_WAIT)
            conn_state = 1;
        else if(rconn->state == CONN_ESTABLISHED || rconn->state == CONN_MCU_ESTABLISHED)
        {
          conn_state = 2;
          conn_info = it->start_time.AsString("hh:mm:ss dd.MM.yyyy");
        }
        rconn->Unlock();
      }

      if(raccount->keep_alive_enable)
      {
        if(raccount->keep_alive_time_response > raccount->keep_alive_time_request-PTimeInterval(raccount->keep_alive_interval*1000-2000))
          ping_state = 1;
        else
          ping_state = 2;
      }

      if(reg_state != 0 && raccount->start_time != PTime(0))
        reg_info = raccount->start_time.AsString("hh:mm:ss dd.MM.yyyy");
      if(ping_state == 2)
        ping_info = raccount->keep_alive_time_response.AsString("hh:mm:ss dd.MM.yyyy");

      list.AppendString(raccount->display_name+" ["+raccount->GetUrl()+"],"+
                        PString(raccount->abook_enable)+","+
                        remote_application+","+
                        PString(reg_state)+","+
                        reg_info+","+
                        PString(conn_state)+","+
                        conn_info+","+
                        PString(ping_state)+","+
                        ping_info
                       );
    }
    if(account_status_list != list)
    {
      account_status_list = list;
      OpenMCU::Current().ManagerRefreshAddressBook();
    }
  }
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void Registrar::SubscriptionThread(PThread &, INT)
{
  while(!terminating)
  {
    PThread::Sleep(1000);
    if(terminating)
      break;

    PTime now;
    for(MCURegistrarSubscriptionList::shared_iterator it = subscriptionList.begin(); it != subscriptionList.end(); ++it)
    {
      RegistrarSubscription *rsub = *it;
      RegSubscriptionStates state_new;
      if(now > rsub->start_time + PTimeInterval(rsub->expires*1000))
      {
        if(subscriptionList.Erase(it))
          delete rsub;
        continue;
      }

      RegistrarAccount *raccount_out = FindAccountWithLock(ACCOUNT_TYPE_SIP, rsub->username_out);
      if(!raccount_out)
        raccount_out = FindAccountWithLock(ACCOUNT_TYPE_H323, rsub->username_out);

      if(raccount_out && raccount_out->registered)
      {
        state_new = SUB_STATE_OPEN;
        if(HasRegConnUsername(raccount_out->username))
          state_new = SUB_STATE_BUSY;
      } else {
        state_new = SUB_STATE_CLOSED;
      }

      // send notify
      if(rsub->state != state_new)
      {
        rsub->state = state_new;
        SipSendNotify(rsub);
      }
      if(raccount_out) raccount_out->Unlock();
    }
  }
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void Registrar::AliveThread(PThread &, INT)
{
  while(!terminating)
  {
    PThread::Sleep(1000);
    if(terminating)
      break;

    PTime now;
    for(MCURegistrarAccountList::shared_iterator it = accountList.begin(); it != accountList.end(); ++it)
    {
      RegistrarAccount *raccount = *it;
      if(raccount->account_type != ACCOUNT_TYPE_SIP)
        continue;
      if(raccount->keep_alive_enable && now > raccount->keep_alive_time_request+PTimeInterval(raccount->keep_alive_interval*1000))
      {
        raccount->keep_alive_time_request = now;
        SipSendPing(raccount);
      }
    }
  }
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void Registrar::QueueThread(PThread &, INT)
{
  while(!terminating)
  {
    PThread::Sleep(100);
    if(terminating)
      break;

    while(1)
    {
      PString *cmd = regQueue.Pop();
      if(cmd == NULL)
        break;
      if(cmd->Left(7) == "invite:")
        QueueInvite(cmd->Right(cmd->GetLength()-7));
      else if(cmd->Left(12) == "established:")
        QueueEstablished(cmd->Right(cmd->GetLength()-12));
      else if(cmd->Left(8) == "cleared:")
        QueueCleared(cmd->Right(cmd->GetLength()-8));
      delete cmd;
    }
  }
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void Registrar::QueueClear()
{
  regQueue.Stop();
  for(;;)
  {
    PString *cmd = regQueue.Pop();
    if(cmd == NULL)
      break;
    delete cmd;
  }
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void Registrar::QueueInvite(const PString & data)
{
  PString from = data.Tokenise(",")[0];
  PString to = data.Tokenise(",")[1];
  PString callToken = data.Tokenise(",")[2];
  MCUURL url(to);
  if(url.GetScheme() == "sip")
  {
    sep->SipMakeCall(from, to, callToken);
  }
  else if(url.GetScheme() == "h323")
  {
    RegistrarConnection *rconn = FindRegConnWithLock(callToken);
    if(rconn == NULL)
      return;
    H323Transport * transport = NULL;
    if(ep->GetGatekeeper())
    {
      PString gk_host = ep->GetGatekeeperHostName();
      if(url.GetHostName() == "" || gk_host == url.GetHostName())
      {
        to = url.GetUserName();
        PTRACE(1, "Found gatekeeper, change address " << url.GetUrl() << " -> " << to);
      }
      else
      {
        H323TransportAddress taddr(url.GetHostName()+":"+url.GetPort());
        transport = taddr.CreateTransport(*ep);
        transport->SetRemoteAddress(taddr);
      }
    }
    PString newToken;
    void *userData = new PString(from);
    if(ep->MakeCall(to, transport, newToken, userData))
    {
      rconn->callToken_in = newToken;
      rconn->callToken_out = newToken;
    }
    rconn->Unlock();
  }
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void Registrar::QueueEstablished(const PString & data)
{
  PString callToken = data;
  RegistrarConnection *rconn = FindRegConnWithLock(callToken);
  if(rconn == NULL)
    return;
  if(rconn->state == CONN_MCU_WAIT)
    rconn->state = CONN_MCU_ESTABLISHED;
  if(rconn->state == CONN_WAIT && rconn->callToken_out == callToken)
    rconn->state = CONN_ACCEPT_IN;
  rconn->Unlock();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void Registrar::QueueCleared(const PString & data)
{
  PString callToken = data;
  RegistrarConnection *rconn = FindRegConnWithLock(callToken);
  if(rconn == NULL)
    return;
  if(rconn->state == CONN_MCU_WAIT || rconn->state == CONN_MCU_ESTABLISHED)
    rconn->state = CONN_IDLE;
  if(rconn->state == CONN_WAIT && rconn->callToken_out == callToken)
    rconn->state = CONN_CANCEL_IN;
  if(rconn->state == CONN_WAIT && rconn->callToken_in == callToken)
    rconn->state = CONN_CANCEL_OUT;
  if(rconn->state == CONN_ESTABLISHED && rconn->callToken_out == callToken)
    rconn->state = CONN_LEAVE_IN;
  if(rconn->state == CONN_ESTABLISHED && rconn->callToken_in == callToken)
    rconn->state = CONN_LEAVE_OUT;
  if(rconn->state == CONN_ACCEPT_IN && rconn->callToken_out == callToken)
    rconn->state = CONN_LEAVE_IN;
  if(rconn->state == CONN_ACCEPT_IN && rconn->callToken_in == callToken)
    rconn->state = CONN_LEAVE_OUT;
  rconn->Unlock();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void Registrar::InitConfig()
{
  MCUConfig cfg("Registrar Parameters");

  // general parameters
  registrar_domain = cfg.GetString("Registrar domain", "openmcu-ru");
  allow_internal_calls = cfg.GetBoolean("Allow internal calls", TRUE);

  enable_gatekeeper = cfg.GetBoolean("H.323 gatekeeper enable", TRUE);
  if(enable_gatekeeper && registrarGk == NULL)
  {
    PIPSocket::Address address("*");
    WORD port = 1719;
    registrarGk = new RegistrarGk(ep, this);
    PString mcuName = OpenMCU::Current().GetName();
    registrarGk->SetGatekeeperIdentifier(mcuName);
    registrarGk->CreateListener(new H323TransportUDP(*ep, address, port, NULL));
  }
  if(!enable_gatekeeper && registrarGk != NULL)
  {
    delete registrarGk;
    registrarGk = NULL;
  }

  // SIP parameters
  sip_require_password = cfg.GetBoolean("SIP registrar required password authorization", FALSE);
  sip_allow_unauth_mcu_calls = cfg.GetBoolean("SIP allow unauthorized MCU calls", TRUE);
  sip_allow_unauth_internal_calls = cfg.GetBoolean("SIP allow unauthorized internal calls", TRUE);
  sip_reg_min_expires = cfg.GetInteger("SIP registrar minimum expiration", 60);
  sip_reg_max_expires = cfg.GetInteger("SIP registrar maximum expiration", 600);

  // H.323 parameters
  h323_require_h235 = cfg.GetBoolean("H.323 gatekeeper required password authorization", FALSE);
  h323_allow_unreg_mcu_calls = cfg.GetBoolean("H.323 allow unregistered MCU calls", TRUE);
  h323_allow_unreg_internal_calls = cfg.GetBoolean("H.323 allow unregistered internal calls", TRUE);
  h323_min_time_to_live = cfg.GetInteger("H.323 gatekeeper minimum Time To Live", 60);
  h323_max_time_to_live = cfg.GetInteger("H.323 gatekeeper maximum Time To Live", 600);
  if(registrarGk)
  {
    registrarGk->SetRequireH235(h323_require_h235);
    registrarGk->SetMinTimeToLive(h323_min_time_to_live);
    registrarGk->SetMaxTimeToLive(h323_max_time_to_live);
    registrarGk->SetTimeToLive(h323_max_time_to_live);
  }
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void Registrar::InitAccounts()
{
  PWaitAndSignal m(mutex);

  MCUConfig cfg("Registrar Parameters");

  PString h323SectionPrefix = "H323 Endpoint ";
  PString rtspSectionPrefix = "RTSP Endpoint ";
  PString sipSectionPrefix = "SIP Endpoint ";

  PStringToString h323Passwords;

  for(MCURegistrarAccountList::shared_iterator it = accountList.begin(); it != accountList.end(); ++it)
  {
    RegistrarAccount *raccount = *it;
    if(raccount->account_type == ACCOUNT_TYPE_SIP)
    {
      raccount->enable = MCUConfig(sipSectionPrefix+raccount->username).GetBoolean("Registrar", FALSE);
      if(!raccount->enable && sip_require_password)
        raccount->registered = FALSE;
    }
    if(raccount->account_type == ACCOUNT_TYPE_H323)
    {
      raccount->enable = MCUConfig(h323SectionPrefix+raccount->username).GetBoolean("Registrar", FALSE);
      if(!raccount->enable && h323_require_h235)
        raccount->registered = FALSE;
    }
  }

  PStringList sect = cfg.GetSections();
  for(PINDEX i = 0; i < sect.GetSize(); i++)
  {
    RegAccountTypes account_type = ACCOUNT_TYPE_UNKNOWN;
    PString username;
    MCUConfig scfg(sect[i]);
    MCUConfig gcfg;
    if(sect[i].Left(sipSectionPrefix.GetLength()) == sipSectionPrefix)
    {
      account_type = ACCOUNT_TYPE_SIP;
      username = sect[i].Right(sect[i].GetLength()-sipSectionPrefix.GetLength());
      gcfg = MCUConfig(sipSectionPrefix+"*");
    }
    else if(sect[i].Left(h323SectionPrefix.GetLength()) == h323SectionPrefix)
    {
      account_type = ACCOUNT_TYPE_H323;
      username = sect[i].Right(sect[i].GetLength()-h323SectionPrefix.GetLength());
      gcfg = MCUConfig(h323SectionPrefix+"*");
    }
    else if(sect[i].Left(h323SectionPrefix.GetLength()) == rtspSectionPrefix)
    {
      account_type = ACCOUNT_TYPE_RTSP;
      username = sect[i].Right(sect[i].GetLength()-rtspSectionPrefix.GetLength());
      gcfg = MCUConfig(rtspSectionPrefix+"*");
    }

    if(username == "*" || username == "")
      continue;

    unsigned port = scfg.GetInteger(PortKey);
    if(port == 0) port = gcfg.GetInteger(PortKey);

    RegistrarAccount *raccount = FindAccountWithLock(account_type, username);
    if(!raccount)
      raccount = InsertAccountWithLock(account_type, username);
    raccount->enable = scfg.GetBoolean("Registrar", FALSE);
    raccount->abook_enable = scfg.GetBoolean("Address book", FALSE);
    raccount->host = scfg.GetString(HostKey);
    if(port != 0)
      raccount->port = port;
    raccount->domain = registrar_domain;
    raccount->password = scfg.GetString(PasswordKey);
    raccount->display_name = scfg.GetString(DisplayNameKey);
    if(account_type == ACCOUNT_TYPE_SIP)
    {
      raccount->keep_alive_interval = scfg.GetString("Ping interval").AsInteger();
      if(raccount->keep_alive_interval == 0)
        raccount->keep_alive_interval = gcfg.GetString("Ping interval").AsInteger();
      if(raccount->keep_alive_interval != 0)
        raccount->keep_alive_enable = TRUE;
      else
        raccount->keep_alive_enable = FALSE;

      PString sip_call_processing = scfg.GetString("SIP call processing");
      if(sip_call_processing == "")
        sip_call_processing = gcfg.GetString("SIP call processing", "redirect");
      raccount->sip_call_processing = sip_call_processing;
    }
    else if(account_type == ACCOUNT_TYPE_H323)
    {
      PString h323_call_processing = scfg.GetString("H.323 call processing");
      if(h323_call_processing == "")
        h323_call_processing = gcfg.GetString("H.323 call processing", "direct");
      raccount->h323_call_processing = h323_call_processing;

      h323Passwords.Insert(PString(username), new PString(raccount->password));
    }
    else if(account_type == ACCOUNT_TYPE_RTSP)
    {
      raccount->abook_enable = TRUE;
    }
    raccount->Unlock();
  }
  // set gatekeeper parameters
  if(registrarGk)
    registrarGk->SetPasswords(h323Passwords);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void Registrar::Terminating()
{
  // stop alive refresh
  if(aliveThread)
  {
    PTRACE(5,"Registrar\tWaiting for termination alive thread: " << aliveThread->GetThreadName());
    aliveThread->WaitForTermination();
    delete aliveThread;
    aliveThread = NULL;
  }

  // stop abook refresh
  if(bookThread)
  {
    PTRACE(5,"Registrar\tWaiting for termination book thread: " << bookThread->GetThreadName());
    bookThread->WaitForTermination();
    delete bookThread;
    bookThread = NULL;
  }

  // stop subscriptions refresh
  if(subscriptionThread)
  {
    PTRACE(5,"Registrar\tWaiting for termination book thread: " << connectionThread->GetThreadName());
    subscriptionThread->WaitForTermination();
    delete subscriptionThread;
    subscriptionThread = NULL;
  }

  // stop queue thread
  QueueClear();
  if(queueThread)
  {
    PTRACE(5,"Registrar\tWaiting for termination queue thread: " << queueThread->GetThreadName());
    queueThread->WaitForTermination();
    delete queueThread;
    queueThread = NULL;
  }

  // stop accounts refresh
  if(accountThread)
  {
    PTRACE(5,"Registrar\tWaiting for termination accounts thread: " << accountThread->GetThreadName());
    accountThread->WaitForTermination();
    delete accountThread;
    accountThread = NULL;
  }

  // stop connections refresh
  if(connectionThread)
  {
    PTRACE(5,"Registrar\tWaiting for termination book thread: " << connectionThread->GetThreadName());
    connectionThread->WaitForTermination();
    delete connectionThread;
    connectionThread = NULL;
  }

  for(MCURegistrarAccountList::shared_iterator it = accountList.begin(); it != accountList.end(); ++it)
  {
    RegistrarAccount *raccount = *it;
    if(accountList.Erase(it))
      delete raccount;
  }
  for(MCURegistrarConnectionList::shared_iterator it = connectionList.begin(); it != connectionList.end(); ++it)
  {
    RegistrarConnection *rconn = *it;
    if(connectionList.Erase(it))
      delete rconn;
  }
  for(MCURegistrarSubscriptionList::shared_iterator it = subscriptionList.begin(); it != subscriptionList.end(); ++it)
  {
    RegistrarSubscription *rsub = *it;
    if(subscriptionList.Erase(it))
      delete rsub;
  }

  if(registrarGk)
  {
    delete registrarGk;
    registrarGk = NULL;
  }
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void Registrar::MainLoop()
{
  while(1)
  {
    PThread::Sleep(100);
    if(terminating)
    {
      Terminating();
      return;
    }
    if(restart)
    {
      restart = 0;
      init_config = 1;
      init_accounts = 1;
    }
    if(init_config)
    {
      init_config = 0;
      InitConfig();
    }
    if(init_accounts)
    {
      init_accounts = 0;
      InitAccounts();
    }
  }
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void Registrar::Main()
{
  accountThread = PThread::Create(PCREATE_NOTIFIER(AccountThread), 0, PThread::NoAutoDeleteThread, PThread::NormalPriority, "registrar account:%0x");
  connectionThread = PThread::Create(PCREATE_NOTIFIER(ConnectionThread), 0, PThread::NoAutoDeleteThread, PThread::NormalPriority, "registrar connection:%0x");
  subscriptionThread = PThread::Create(PCREATE_NOTIFIER(SubscriptionThread), 0, PThread::NoAutoDeleteThread, PThread::NormalPriority, "registrar subscription:%0x");
  queueThread = PThread::Create(PCREATE_NOTIFIER(QueueThread), 0, PThread::NoAutoDeleteThread, PThread::NormalPriority, "registrar queue:%0x");
  aliveThread = PThread::Create(PCREATE_NOTIFIER(AliveThread), 0, PThread::NoAutoDeleteThread, PThread::NormalPriority, "registrar alive:%0x");
  bookThread = PThread::Create(PCREATE_NOTIFIER(BookThread), 0, PThread::NoAutoDeleteThread, PThread::NormalPriority, "registrar book:%0x");

  MainLoop();
}

////////////////////////////////////////////////////////////////////////////////////////////////////
