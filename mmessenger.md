
# MIDAS Messenger (mmessenger)

This tool has been developed to integrate with Discord, Slack and Mattermost webhooks. It simply relays MIDAS messages

Running the program will create odb entries in
```
/MIDASMessenger/Discord/WebHooks
/MIDASMessenger/Slack/WebHooks
/MIDASMessenger/Mattermost/WebHooks
```
within each of these are 6 strings (by default bank and therefor do nothing)
```
MT_ERROR
MT_INFO	
MT_DEBUG
MT_USER	
MT_LOG	
MT_TALK	
MT_CALL
```

One simply needs to paste in the webhook link in the categories you want, then restart the MIDAS Messenger. 
They message types have been separated so that uses can direct differn't message types to differnt channels (alternatively users can also direct everything to the same place by using the same webhook URL multiple times).

### Webhook documentation can be found below:

##### Discord
[https://support.discord.com/hc/en-us/articles/228383668-Intro-to-Webhooks](https://support.discord.com/hc/en-us/articles/228383668-Intro-to-Webhooks)

##### Slack
[https://api.slack.com/messaging/webhooks](https://api.slack.com/messaging/webhooks)

###### Mattermast
[https://docs.mattermost.com/developer/webhooks-incoming.html#simple-incoming-webhook](https://docs.mattermost.com/developer/webhooks-incoming.html#simple-incoming-webhook)


