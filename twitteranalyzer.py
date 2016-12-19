import sys
import requests
import json
import twitter
import config
import pprint

def convert_status_to_pi_content_item(s):
    # My code here
    return {
        'userid': str(s.user.id),
        'id': str(s.id),
        'sourceid': 'python-twitter',
        'contenttype': 'text/plain',
        'language': s.lang,
        'content': s.text,
        'created': s.created_at_in_seconds,
        'reply': (s.in_reply_to_status_id == None),
        'forward': False
    }

def ma(t_id):
    handle = t_id#sys.argv[1]

    twitter_api = twitter.Api(consumer_key='DIHaMzuF37zYvCsIwGUee3QO6',
                              consumer_secret='oUotOFsH8kiz2Neh35bxH2lu5El94GLtOQIIllK8hjlmcpmvM6',
                              access_token_key='807677389878726657-x0IGggAGUrg1FJeblrUC8u56BpcaXXt',
                              access_token_secret='2NxlriEmgcpm5lSiLJsAbneWrlgXoZpyHYlwAF9hrbX9q',
                              debugHTTP=True)

    max_id = None
    statuses = []
    for x in range(0, 16):  # Pulls max number of tweets from an account
        if x == 0:
            statuses_portion = twitter_api.GetUserTimeline(screen_name=handle,
                                                           count=200,
                                                           include_rts=False)
            status_count = len(statuses_portion)
            max_id = statuses_portion[status_count - 1].id - 1  # get id of last tweet and bump below for next tweet set
        else:
            statuses_portion = twitter_api.GetUserTimeline(screen_name=handle,
                                                           count=200,
                                                           max_id=max_id,
                                                           include_rts=False)
            status_count = len(statuses_portion)
            if status_count!=0 :
                max_id = statuses_portion[status_count - 1].id - 1  # get id of last tweet and bump below for next tweet set
        for status in statuses_portion:
            statuses.append(status)

    pi_content_items_array = map(convert_status_to_pi_content_item, statuses)
    pi_content_items = {'contentItems': pi_content_items_array}

    r = requests.post('https://gateway.watsonplatform.net/personality-insights/api' + '/v2/profile',
                      auth=('c5032e9c-1eef-48a6-b237-93d8e6153d9e', 'hQLicY30CvOs'),
                      headers={
                          'content-type': 'application/json',
                          'accept': 'application/json'
                      },
                      data=json.dumps(pi_content_items)
                      )

    print("Profile Request sent. Status code: %d, content-type: %s" % (r.status_code, r.headers['content-type']))
    pprint.pprint(json.loads(r.text))
    jsonFile = open("outcome.json", "w")
    jsonFile.write(json.dumps(json.loads(r.text), indent=4, separators=(',', ': ')))
    jsonFile.close()

ma('KingJames')
