#!/bin/sh
case " $RENEWED_DOMAINS " in
    *" ai-passport.randomdance.cn "*) /usr/bin/nginx -t && /usr/bin/nginx -s reload ;;
esac
