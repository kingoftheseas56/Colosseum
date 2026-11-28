import importlib.util
import pathlib
import socket
import unittest
from unittest.mock import Mock, patch

ROOT = pathlib.Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('browse_provider', ROOT / 'scripts/vinyl/provider.py')
provider = importlib.util.module_from_spec(spec)
spec.loader.exec_module(provider)
CHANNEL = 'UCbRSywya_rl8YS15Lo9ttsA'
PARAM = 'ggMPOg1uXzVLbmZnaWI4STNs'
VIDEO = 'KCMCPsUpD3w'
ATV = 'audioTrack1'
OMV = 'musicVideo1'


def endpoint(key, page):
    return {'browseEndpoint': {'browseId': key, 'browseEndpointContextSupportedConfigs': {
        'browseEndpointContextMusicConfig': {'pageType': 'MUSIC_PAGE_TYPE_' + page}}}}


def song_row(video=VIDEO, video_type='MUSIC_VIDEO_TYPE_ATV'):
    watch = {'watchEndpoint': {'videoId': video, 'watchEndpointMusicSupportedConfigs': {
        'watchEndpointMusicConfig': {'musicVideoType': video_type}}}}
    return {'musicResponsiveListItemRenderer': {
        'flexColumns': [{'musicResponsiveListItemFlexColumnRenderer': {'text': {'runs': runs}}} for runs in (
            [{'text': 'Source song', 'navigationEndpoint': watch}],
            [{'text': 'Source artist', 'navigationEndpoint': endpoint(CHANNEL, 'ARTIST')}])],
        'overlay': {'musicItemThumbnailOverlayRenderer': {'content': {
            'musicPlayButtonRenderer': {'playNavigationEndpoint': watch}}}}}}


def two_row(key='MPRE_source', page='ALBUM'):
    return {'musicTwoRowItemRenderer': {'title': {'runs': [
        {'text': 'Source album', 'navigationEndpoint': endpoint(key, page)}]},
        'navigationEndpoint': endpoint(key, page), 'subtitle': {'runs': [
            {'text': 'Album'}, {'text': ' · '},
            {'text': 'Source artist', 'navigationEndpoint': endpoint(CHANNEL, 'ARTIST')}]},
        'thumbnailRenderer': {'musicThumbnailRenderer': {'thumbnail': {'thumbnails': []}}}}}


def shelf(rows, token=''):
    value = {'contents': rows}
    if token:
        value['continuations'] = [{'nextContinuationData': {'continuation': token}}]
    return {'musicCarouselShelfRenderer': value}


class BrowseProviderTests(unittest.TestCase):
    def setUp(self):
        self.client = Mock()
        self.client.get_explore.return_value = {'trending': {'items': []}, 'new_releases': []}
        self.client.get_charts.return_value = {'countries': {'options': ['ZZ', 'IN', 'US']}, 'artists': []}

    def browse(self, kind='songs', **options):
        return provider.handle(dict(operation='browseCatalogue', value=kind, **options), client=self.client)

    def test_youtube_client_requests_english_without_overriding_an_explicit_market(self):
        constructor = Mock()
        with patch.dict('sys.modules', {'ytmusicapi': Mock(YTMusic=constructor)}):
            for region, location in (('', 'US'), ('ZZ', 'US'), ('FR', 'FR')):
                with self.subTest(region=region):
                    self.assertIs(provider._ytmusic_client(region, reuse=False), constructor.return_value)
                    constructor.assert_called_with(language='en', location=location)

    def test_defaults_are_finite_and_register_playable_source_records_without_hydration(self):
        raw = {'videoId': VIDEO, 'title': 'Source song', 'artists': [{'id': CHANNEL, 'name': 'Source artist'}],
               'videoType': 'MUSIC_VIDEO_TYPE_ATV', 'duration_seconds': 190}
        self.client.get_explore.return_value['trending']['items'] = [raw, raw, dict(raw, videoType='MUSIC_VIDEO_TYPE_PODCAST_EPISODE')]
        page = self.browse()
        self.assertTrue(page['complete'])
        self.assertEqual(page['nextCursor'], {})
        self.assertEqual(len(page['items']), 1)
        item, record = page['items'][0], page['records'][0]
        self.assertEqual(item['albumId'], record['id'])
        self.assertEqual(item['trackId'], record['tracks'][0]['id'])
        self.assertEqual(item['trackIndex'], 0)
        self.assertEqual(record['tracks'][0]['videoId'], VIDEO)
        self.assertEqual(record['tracks'][0]['artistIds'], ['ytm:artist:' + CHANNEL])
        self.client.get_album.assert_not_called()
        self.client.get_artist.assert_not_called()

    def test_album_row_keeps_the_audio_and_official_video_endpoints_separate(self):
        title = 'Many Men'
        row = song_row(OMV, 'MUSIC_VIDEO_TYPE_OMV')
        renderer = row['musicResponsiveListItemRenderer']
        # Album's thumbnail plays the OMV; the row's title points to its ATV.
        renderer['flexColumns'][0] = song_row(ATV)['musicResponsiveListItemRenderer']['flexColumns'][0]
        self.client._send_request.return_value = {'secondaryContents': {'musicShelfRenderer': {'contents': [row]}}}

        def album(key):
            self.client._send_request('browse', {'browseId': key})
            # ytmusicapi gets the ID and type from different parts of the row.
            return {'title': 'Get Rich or Die Tryin\'', 'tracks': [{
                'videoId': OMV, 'videoType': 'MUSIC_VIDEO_TYPE_ATV', 'title': title,
                'artists': [{'id': CHANNEL, 'name': '50 Cent'}], 'duration_seconds': 257}]}

        self.client.get_album.side_effect = album
        record = provider.handle({'operation': 'album', 'value': 'MPRE_album'}, client=self.client)
        track = record['tracks'][0]
        self.assertEqual((track['videoId'], track['videoType'], track['seconds']), (OMV, 'MUSIC_VIDEO_TYPE_OMV', 257))
        self.client._send_request.assert_called_once()

        with patch.object(provider, 'resolve', side_effect=lambda video: {'videoId': video}) as extract:
            audio = provider.handle({'operation': 'resolve', 'value': track['audioVideoId']}, client=self.client)
        self.assertEqual(audio['videoId'], ATV)
        self.assertEqual(track['musicVideoId'], OMV)
        extract.assert_called_once_with(ATV)
        self.client.get_watch_playlist.assert_not_called()

        self.client.get_song.return_value = {
            'videoDetails': {'videoId': OMV, 'musicVideoType': 'MUSIC_VIDEO_TYPE_OMV', 'title': 'Many Men (Official Video)'},
            'playabilityStatus': {'status': 'OK'}}
        candidate = provider.handle({'operation': 'relatedVideo', 'value': track['videoId'],
                                     'musicVideoId': track['musicVideoId'], 'title': title}, client=self.client)
        self.assertEqual((candidate['status'], candidate['videoId']), ('ready', OMV))
        self.assertFalse(candidate['sameMediaClock'])
        with patch.object(provider, 'resolve_video', side_effect=lambda video: {'videoId': video}) as extract_video:
            provider.handle({'operation': 'resolveVideo', 'value': candidate['videoId']}, client=self.client)
        extract_video.assert_called_once_with(OMV)

    def test_audio_fallback_prefers_matching_duration_to_official_clip(self):
        artists = [{'id': CHANNEL, 'name': '50 Cent'}]
        row = {'videoId': OMV, 'title': 'Many Men', 'artists': artists, 'duration_seconds': 257,
               'videoType': 'MUSIC_VIDEO_TYPE_OMV'}
        self.client.get_watch_playlist.return_value = {'tracks': [row]}
        target = dict(row, videoId=ATV, videoType=None, duration_seconds=259)
        self.client.search.return_value = [dict(row, duration_seconds=297),
                                           dict(target, videoId=VIDEO, duration_seconds=261), target]
        track = provider.song_record(row)['tracks'][0]
        request = {'operation': 'resolve', 'value': OMV, 'audioTrack': track}
        with patch.object(provider, 'resolve', side_effect=lambda video: {'videoId': video}) as extract:
            self.assertEqual(provider.handle(request, client=self.client), {'videoId': ATV, 'sourceVideoId': OMV})
            extract.assert_called_once_with(ATV)
            self.client.search.return_value.pop()
            self.assertEqual(provider.handle(request, client=self.client), {'videoId': OMV})
        self.client.search.assert_called_with('Many Men 50 Cent', filter='songs', limit=10)

    def test_audio_fallback_uses_only_a_counterpart_paired_to_the_requested_id(self):
        raw = {'videoId': ATV, 'videoType': 'MUSIC_VIDEO_TYPE_ATV', 'title': 'Many Men',
               'counterpart': {'videoId': OMV, 'videoType': 'MUSIC_VIDEO_TYPE_OMV'}}
        track = provider.song_record(raw)['tracks'][0]
        self.assertEqual((track['audioVideoId'], track['musicVideoId']), (ATV, OMV))
        self.client.get_watch_playlist.return_value = {'tracks': [raw]}
        self.assertEqual(provider.choose_audio_video_id(self.client, OMV, {}, ''), ATV)
        self.assertEqual(provider.choose_audio_video_id(self.client, VIDEO, {}, ''), VIDEO)
        self.client.search.assert_not_called()

    def test_categories_preserve_returned_parameters_and_exclude_nonmusic_duplicates(self):
        self.client.get_mood_categories.return_value = {'Genres': [{'title': 'Rock', 'params': PARAM},
            {'title': 'Rock repeat', 'params': PARAM}], 'Podcasts': [{'title': 'Business', 'params': 'business'}]}
        page = provider.handle({'operation': 'browseCategories', 'value': ''}, client=self.client)
        self.assertEqual(page['source'], 'youtube-music')
        self.assertEqual([(v['title'], v['params'], v['group']) for v in page['categories']], [('Rock', PARAM, 'Genres')])
        self.assertTrue(page['categories'][0]['id'])

    def test_mixed_category_shelves_select_real_music_types_without_search(self):
        self.client._send_request.return_value = {'sections': [shelf([song_row(), song_row(),
            song_row('abcdefghijk', 'MUSIC_VIDEO_TYPE_PODCAST_EPISODE'), two_row(), two_row('VLplaylist', 'PLAYLIST')])]}
        songs = self.browse(category=PARAM)
        albums = self.browse('albums', category=PARAM)
        self.assertEqual(len(songs['items']), 1)
        self.assertEqual(len(albums['items']), 1)
        self.assertEqual(albums['records'][0]['browseId'], 'MPRE_source')
        self.assertEqual(songs['records'][0]['tracks'][0]['videoId'], VIDEO)
        self.assertTrue(songs['complete'])
        self.client.search.assert_not_called()
        self.client.get_mood_playlists.assert_not_called()
        self.client._send_request.assert_called_with('browse', {'browseId': 'FEmusic_moods_and_genres_category', 'params': PARAM})

    def test_real_continuation_is_bound_and_duplicate_or_repeated_pages_fail(self):
        self.client._send_request.return_value = {'sections': [shelf([song_row()], 'real_source_token')]}
        first = self.browse(category=PARAM)
        self.assertFalse(first['complete'])
        cursor = first['nextCursor']
        with self.assertRaises(provider.InvalidArgumentError):
            self.browse('albums', category=PARAM, cursor=cursor)
        with self.assertRaises(provider.InvalidArgumentError):
            self.browse(category='another_category', cursor=cursor)
        self.client._send_request.return_value = {'continuationContents': {'musicShelfContinuation': {
            'contents': [song_row('abcdefghijk')]}}}
        second = self.browse(category=PARAM, cursor=cursor)
        self.assertEqual(second['offset'], 1)
        self.assertTrue(second['complete'])
        self.client._send_request.assert_called_with('browse', {'continuation': 'real_source_token'})
        self.client._send_request.return_value = {'continuationContents': {'musicShelfContinuation': {
            'contents': [song_row()]}}}
        with self.assertRaises(provider.ProviderUnavailableError):
            self.browse(category=PARAM, cursor=cursor)
        self.client._send_request.return_value = {'continuationContents': {'musicShelfContinuation': {
            'contents': [song_row()], 'continuations': [{'nextContinuationData': {'continuation': 'real_source_token'}}]}}}
        with self.assertRaises(provider.ProviderUnavailableError):
            self.browse(category=PARAM, cursor=cursor)

    def test_artists_use_explicit_or_global_charts_with_no_invented_rank(self):
        self.client.get_charts.return_value['artists'] = [{'title': 'Source artist', 'browseId': CHANNEL, 'rank': None}]
        page = self.browse('artists', region='ZZ')
        self.client.get_charts.assert_called_once_with('ZZ')
        self.assertEqual(page['items'][0]['artistId'], 'ytm:artist:' + CHANNEL)
        self.assertNotIn('rank', page['items'][0])
        self.assertEqual([v['code'] for v in page['markets']], ['ZZ', 'IN', 'US'])
        self.assertEqual(provider.ytmusic_location_options('ZZ'), {})
        with self.assertRaises(provider.InvalidArgumentError):
            self.browse('artists', region='XX')

    def test_invalid_scope_options_and_cursors_are_rejected_before_network(self):
        for kind, options in [('videos', {}), ('artists', {'category': PARAM}), ('songs', {'region': 'IN'}),
            ('songs', {'category': 'https://invalid'}), ('songs', {'cursor': []}),
            ('songs', {'cursor': {'token': 'forged'}}), ('albums', {'genre': 'rock'}), ('artists', {}),
            ([], {}), ('songs', {'category': 'x' * 2049}), ('songs', {'refresh': 'true'})]:
            with self.subTest(kind=kind, options=options), self.assertRaises(provider.InvalidArgumentError):
                self.browse(kind, **options)
        self.assertEqual(self.client.mock_calls, [])

    def test_empty_and_malformed_provider_results_are_distinct(self):
        self.assertEqual(self.browse()['items'], [])
        self.client._send_request.return_value = {'sections': [shelf([two_row('VLplaylist', 'PLAYLIST')])]}
        self.assertEqual(self.browse(category=PARAM)['items'], [])
        self.client.get_explore.side_effect = socket.timeout('timed out')
        with self.assertRaises(provider.ProviderTimeoutError):
            self.browse()
        self.client.get_explore.side_effect = None
        self.client.get_explore.return_value = {'trending': None}
        with self.assertRaises(provider.ProviderUnavailableError):
            self.browse()
        self.client._send_request.return_value = {'unexpected': True}
        with self.assertRaises(provider.ProviderUnavailableError):
            self.browse(category=PARAM)

    def test_browse_cards_never_return_signed_media_as_artwork(self):
        self.client.get_explore.return_value['new_releases'] = [{'browseId': 'MPRE_source', 'title': 'Album',
            'thumbnails': [{'url': 'https://r1.googlevideo.com/videoplayback?sig=secret', 'width': 1000}]}]
        page = self.browse('albums')
        self.assertEqual(page['items'][0]['cover'], '')
        self.assertEqual(page['records'][0]['cover'], '')


if __name__ == '__main__':
    unittest.main()
